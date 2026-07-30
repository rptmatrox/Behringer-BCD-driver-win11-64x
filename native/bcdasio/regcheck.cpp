// regcheck: PROVA que o nosso registro do driver (native/bcdasio/asioreg.cpp) escreve no
// registro do Windows exatamente o que o register.cpp da Steinberg escrevia.
//
// FERRAMENTA DE VERIFICACAO LOCAL. Nao entra no BcdAsio.dll, nao faz parte do alvo `all`,
// e exige o SDK no disco - tem o mesmo prazo de validade do abicheck: so roda enquanto o
// SDK estiver aqui.
//
//     build.bat regcheck  &&  regcheck.exe        (-v lista os despejos inteiros)
//
// POR QUE ESTA PROVA EXISTE, E POR QUE REVISAO NAO SERVIA
// O registro de um driver ASIO sao cinco valores em duas arvores. Errar um deles nao
// quebra build nenhuma e nao produz uma linha de log: o driver simplesmente nao aparece na
// lista do software de DJ, ou aparece e falha ao carregar. E ha pelo menos quatro formas de
// errar que NENHUMA revisao de codigo pega:
//
//   * o cbData do valor - com ou sem o terminador da cadeia. Os dois "parecem" certos no
//     regedit, que mostra a mesma coisa;
//   * a CAIXA do caminho do DLL. O codigo antigo gravava em minusculas, e a evidencia que o
//     dono tem na maquina esta assim;
//   * o TIPO do valor (REG_SZ contra REG_EXPAND_SZ) - identicos no regedit;
//   * a DECISAO de quando reescrever e quando apagar a subarvore antiga, que e onde vive a
//     unica logica de verdade do arquivo e que so aparece na SEGUNDA instalacao.
//
// COMO A PROVA E FEITA SEM TOCAR NO REGISTRO DA MAQUINA
// `RegOverridePredefKey` desvia HKEY_CLASSES_ROOT e HKEY_LOCAL_MACHINE, DENTRO DESTE
// PROCESSO SO, para duas subarvores de teste em HKEY_CURRENT_USER: uma para o lado do SDK,
// outra para o nosso. Cada cenario prepara as duas subarvores com o MESMO estado inicial,
// chama uma funcao em cada uma, e compara as duas arvores resultantes chave por chave,
// valor por valor, tipo por tipo e BYTE por byte.
//
// Tres consequencias, e as tres importam:
//   1. nao precisa de elevacao - as subarvores sao em HKCU;
//   2. roda numa maquina com o driver instalado e EM USO, sem risco. O ultimo par de
//      verificacoes confere isso e nao presume: as chaves de verdade do driver sao
//      despejadas antes e depois de tudo, e qualquer diferenca e FALHA;
//   3. exercita o codigo de PRODUCAO. O nosso asioreg.cpp usa HKEY_CLASSES_ROOT e
//      HKEY_LOCAL_MACHINE literais, como na producao - nao existe um "modo de teste" com
//      chaves parametrizadas que poderia estar certo enquanto a producao esta errada.
//
// O QUE ESTA PROVA NAO COBRE, e e melhor estar escrito
//   * a variante ANSI. O register.cpp e compilado sem UNICODE e passa por RegSetValueExA,
//     que converte para UTF-16 pela pagina de codigo do sistema; o nosso e Unicode direto.
//     Para um caminho ASCII - o caso desta maquina, e o caso de qualquer instalacao em
//     Program Files - os dois dao os MESMOS bytes, e e isso que estes cenarios provam com
//     o caminho de verdade. Para um caminho com caractere fora da pagina de codigo ANSI,
//     os dois divergem: o lado do SDK o corrompe, o nosso nao. Essa divergencia e uma
//     melhoria, nao pode ser "provada igual", e por isso nao ha cenario para ela;
//   * permissao. Os dois lados escrevem em HKCU aqui, onde ninguem e negado. O que
//     acontece quando o HKLM real nega escrita e diferente nos dois (o SDK ignorava a
//     falha e devolvia sucesso; o nosso devolve codigo de erro) e isso e melhoria
//     deliberada, tratada no comentario de asioreg.cpp.

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "asioreg.h"

// O register.cpp da Steinberg. As assinaturas sao as dele, com `char*` nao-const e o CLSID
// por valor - copiadas do proprio arquivo, e sao as mesmas que bcdasio.cpp declarava antes
// da etapa C2.
extern LONG RegisterAsioDriver(CLSID, char*, char*, char*, char*);
extern LONG UnregisterAsioDriver(CLSID, char*, char*);

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// O objeto do teste: o CLSID e os textos DE VERDADE do driver.
//
// Nada de GUID inventado nem de nome de fantasia. Um cenario com dados de brinquedo
// provaria que as duas implementacoes concordam sobre dados de brinquedo; o que precisa
// ser provado e que elas concordam sobre a entrada exata que a maquina do dono tem.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
static const CLSID kClsid = { 0xb0d3000a, 0x51e7, 0x4c2b,
                              { 0x9f, 0x3a, 0x12, 0x34, 0xab, 0xcd, 0x56, 0x78 } };
static const wchar_t* const kClsidText   = L"{B0D3000A-51E7-4C2B-9F3A-1234ABCD5678}";
static const wchar_t* const kRegNameW    = L"Behringer BCD3000";
static const wchar_t* const kDescW       = L"Behringer BCD3000 ASIO Driver";
static const wchar_t* const kModelW      = L"Apartment";

// As mesmas cadeias em ANSI, nao-const porque a assinatura do SDK pede `char*`.
static char kRegNameA[] = "Behringer BCD3000";
static char kDescA[]    = "Behringer BCD3000 ASIO Driver";
static char kModelA[]   = "Apartment";

static const wchar_t* const kTestRoot = L"Software\\BcdAsioRegCheck";

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Contabilidade
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
static int  g_returnChecks = 0;   // as duas funcoes disseram que deu certo
static int  g_treeChecks   = 0;   // as duas arvores sao identicas
static int  g_safetyChecks = 0;   // o registro de verdade da maquina nao foi tocado
static int  g_fail         = 0;
static int  g_scenarios    = 0;
static bool g_verbose      = false;

static void pass(int* bucket, const char* label)
{
    (*bucket)++;
    if (g_verbose)
        printf("  ok      %s\n", label);
}

static void fail(int* bucket, const char* label, const char* detail)
{
    (*bucket)++;
    g_fail++;
    printf("  FALHA   %s\n", label);
    if (detail && detail[0])
        printf("          %s\n", detail);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Despejo canonico de uma subarvore
//
// Sai como texto, e o texto e o que se compara. As chaves e os valores sao ORDENADOS pelo
// nome antes de sair: a ordem de enumeracao do registro e ordem de insercao, e comparar por
// ela transformaria "escrevi CLSID antes de Description" num requisito - que nao e. O que e
// requisito e o CONJUNTO de chaves, valores, tipos e bytes, e e isso que este despejo
// mostra.
//
// Os bytes saem em hexadecimal E o texto sai ao lado. O hexadecimal e o que decide: ele
// pega o terminador que falta, que e justamente a diferenca que o texto esconderia.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

enum {
    kDumpMax   = 65536,
    kNameMax   = 512,
    kDataMax   = 4096,
    kEntryMax  = 64        // subchaves ou valores por chave
};

struct Dump {
    char text[kDumpMax];
    int  used;
    bool overflow;
};

static void dumpInit(Dump* d)
{
    d->text[0]  = 0;
    d->used     = 0;
    d->overflow = false;
}

static void dumpAdd(Dump* d, const char* fmt, ...)
{
    if (d->overflow)
        return;
    va_list args;
    va_start(args, fmt);
    const int room = kDumpMax - d->used - 1;
    const int n = _vsnprintf(d->text + d->used, (size_t)room, fmt, args);
    va_end(args);
    if (n < 0 || n >= room) {
        d->overflow = true;
        d->text[kDumpMax - 1] = 0;
        return;
    }
    d->used += n;
    d->text[d->used] = 0;
}

// Ordena nomes com wcscmp. Insercao: sao no maximo kEntryMax por chave e a clareza vale
// mais que a ordem de grandeza aqui.
static void sortNames(wchar_t names[][kNameMax], int count)
{
    for (int i = 1; i < count; i++) {
        wchar_t tmp[kNameMax];
        wcscpy(tmp, names[i]);
        int j = i - 1;
        while (j >= 0 && wcscmp(names[j], tmp) > 0) {
            wcscpy(names[j + 1], names[j]);
            j--;
        }
        wcscpy(names[j + 1], tmp);
    }
}

static void dumpKey(Dump* d, HKEY parent, const wchar_t* subKey, const wchar_t* shownPath)
{
    HKEY key;
    if (RegOpenKeyExW(parent, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        dumpAdd(d, "[%ls]  AUSENTE\n", shownPath);
        return;
    }
    dumpAdd(d, "[%ls]\n", shownPath);

    // ---- valores ----
    wchar_t valueNames[kEntryMax][kNameMax];
    int valueCount = 0;
    for (DWORD i = 0; ; i++) {
        wchar_t name[kNameMax];
        DWORD   cch = kNameMax;
        const LONG rc = RegEnumValueW(key, i, name, &cch, 0, 0, 0, 0);
        if (rc == ERROR_NO_MORE_ITEMS)
            break;
        if (rc != ERROR_SUCCESS) {
            dumpAdd(d, "  <erro %ld ao enumerar valores>\n", rc);
            break;
        }
        if (valueCount >= kEntryMax) {
            dumpAdd(d, "  <mais de %d valores: o despejo nao cabe>\n", (int)kEntryMax);
            break;
        }
        wcscpy(valueNames[valueCount++], name);
    }
    sortNames(valueNames, valueCount);

    for (int i = 0; i < valueCount; i++) {
        DWORD type  = 0;
        BYTE  data[kDataMax];
        DWORD bytes = kDataMax;
        const LONG rc = RegQueryValueExW(key, valueNames[i], 0, &type, data, &bytes);
        if (rc != ERROR_SUCCESS) {
            dumpAdd(d, "  valor \"%ls\": <erro %ld>\n", valueNames[i], rc);
            continue;
        }
        dumpAdd(d, "  valor \"%ls\" tipo=%lu cbData=%lu bytes=",
                valueNames[i][0] ? valueNames[i] : L"(padrao)",
                (unsigned long)type, (unsigned long)bytes);
        for (DWORD b = 0; b < bytes; b++)
            dumpAdd(d, "%02X", data[b]);

        // O texto ao lado e so para leitura humana, e sai de uma COPIA terminada a forca:
        // um REG_SZ gravado sem terminador nao pode ser impresso direto do buffer.
        if ((type == REG_SZ || type == REG_EXPAND_SZ) && bytes >= 2 &&
            bytes + 2 <= kDataMax) {
            data[bytes]     = 0;
            data[bytes + 1] = 0;
            dumpAdd(d, "  texto=\"%ls\"", (const wchar_t*)data);
        }
        dumpAdd(d, "\n");
    }

    // ---- subchaves ----
    wchar_t subNames[kEntryMax][kNameMax];
    int subCount = 0;
    for (DWORD i = 0; ; i++) {
        wchar_t name[kNameMax];
        DWORD   cch = kNameMax;
        const LONG rc = RegEnumKeyExW(key, i, name, &cch, 0, 0, 0, 0);
        if (rc == ERROR_NO_MORE_ITEMS)
            break;
        if (rc != ERROR_SUCCESS) {
            dumpAdd(d, "  <erro %ld ao enumerar subchaves>\n", rc);
            break;
        }
        if (subCount >= kEntryMax) {
            dumpAdd(d, "  <mais de %d subchaves: o despejo nao cabe>\n", (int)kEntryMax);
            break;
        }
        wcscpy(subNames[subCount++], name);
    }
    sortNames(subNames, subCount);

    for (int i = 0; i < subCount; i++) {
        wchar_t child[kNameMax * 2];
        _snwprintf(child, kNameMax * 2 - 1, L"%ls\\%ls", shownPath, subNames[i]);
        child[kNameMax * 2 - 1] = 0;
        dumpKey(d, key, subNames[i], child);
    }

    RegCloseKey(key);
}

// Numero da primeira linha em que dois despejos divergem, com as duas linhas.
static void firstDifference(const char* a, const char* b, char* out, size_t cch)
{
    int line = 1;
    const char* pa = a;
    const char* pb = b;
    while (*pa && *pb && *pa == *pb) {
        if (*pa == '\n')
            line++;
        pa++;
        pb++;
    }
    char lineA[400];
    char lineB[400];
    size_t n = 0;
    while (n < sizeof(lineA) - 1 && pa[n] && pa[n] != '\n') { lineA[n] = pa[n]; n++; }
    lineA[n] = 0;
    n = 0;
    while (n < sizeof(lineB) - 1 && pb[n] && pb[n] != '\n') { lineB[n] = pb[n]; n++; }
    lineB[n] = 0;
    _snprintf(out, cch - 1, "primeira diferenca na linha %d:\n"
                            "            SDK   ...%s\n"
                            "            nosso ...%s", line, lineA, lineB);
    out[cch - 1] = 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Preparacao das subarvores de teste
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void setStringAt(HKEY root, const wchar_t* path, const wchar_t* name,
                        const wchar_t* value)
{
    HKEY key;
    if (RegCreateKeyExW(root, path, 0, 0, 0, KEY_WRITE, 0, &key, 0) != ERROR_SUCCESS)
        return;
    const DWORD bytes = (DWORD)((wcslen(value) + 1) * sizeof(wchar_t));
    RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value, bytes);
    RegCloseKey(key);
}

static void createAt(HKEY root, const wchar_t* path)
{
    HKEY key;
    if (RegCreateKeyExW(root, path, 0, 0, 0, KEY_WRITE, 0, &key, 0) == ERROR_SUCCESS)
        RegCloseKey(key);
}

static void deleteTestRoot()
{
    HKEY software;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0,
                      DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE | KEY_SET_VALUE,
                      &software) != ERROR_SUCCESS)
        return;
    RegDeleteTreeW(software, L"BcdAsioRegCheck");
    RegCloseKey(software);
}

// Abre (criando) HKCU\Software\BcdAsioRegCheck\<side>\<hive>.
static HKEY openSideHive(const wchar_t* side, const wchar_t* hive)
{
    wchar_t path[512];
    _snwprintf(path, 511, L"%ls\\%ls\\%ls", kTestRoot, side, hive);
    path[511] = 0;
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, 0, 0,
                        KEY_READ | KEY_WRITE, 0, &key, 0) != ERROR_SUCCESS)
        return 0;
    return key;
}

// O estado inicial que TODA maquina Windows tem, e sem o qual a comparacao seria sobre uma
// maquina que nao existe: HKCR\CLSID e HKLM\SOFTWARE ja estao la.
//
// A diferenca importa: o createRegPath do SDK abre a chave PAI e falha calado se ela nao
// existir, enquanto o nosso createKey cria as intermediarias. Num HKCR sem CLSID os dois
// divergiriam - o SDK nao registraria nada e nos registrariamos - e o cenario seria sobre
// uma condicao impossivel. Reproduzir a maquina de verdade e o que faz a comparacao valer.
static void seedSkeleton(HKEY hkcr, HKEY hklm)
{
    createAt(hkcr, L"CLSID");
    createAt(hklm, L"SOFTWARE");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Um cenario
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// O que cada lado faz depois do estado inicial. `sdkSide` diz qual implementacao chamar.
typedef LONG (*RunFn)(bool sdkSide, const wchar_t* modulePathW, char* modulePathA);

// Semeadura especifica do cenario, aplicada IGUAL nos dois lados.
typedef void (*SeedFn)(HKEY hkcr, HKEY hklm, const wchar_t* lowerPathW);

static bool overrideTo(HKEY hkcr, HKEY hklm)
{
    if (RegOverridePredefKey(HKEY_CLASSES_ROOT, hkcr) != ERROR_SUCCESS)
        return false;
    if (RegOverridePredefKey(HKEY_LOCAL_MACHINE, hklm) != ERROR_SUCCESS) {
        RegOverridePredefKey(HKEY_CLASSES_ROOT, 0);
        return false;
    }
    return true;
}

static void overrideOff()
{
    RegOverridePredefKey(HKEY_CLASSES_ROOT, 0);
    RegOverridePredefKey(HKEY_LOCAL_MACHINE, 0);
}

static void runScenario(const char* name, SeedFn seed, RunFn run,
                        const wchar_t* modulePathW, const wchar_t* lowerPathW,
                        char* modulePathA)
{
    g_scenarios++;
    printf("\n-- cenario %d: %s\n", g_scenarios, name);

    deleteTestRoot();

    HKEY aCr = openSideHive(L"A", L"HKCR");
    HKEY aLm = openSideHive(L"A", L"HKLM");
    HKEY bCr = openSideHive(L"B", L"HKCR");
    HKEY bLm = openSideHive(L"B", L"HKLM");
    if (!aCr || !aLm || !bCr || !bLm) {
        fail(&g_treeChecks, name, "nao foi possivel criar as subarvores de teste em HKCU");
        return;
    }

    seedSkeleton(aCr, aLm);
    seedSkeleton(bCr, bLm);
    if (seed) {
        seed(aCr, aLm, lowerPathW);
        seed(bCr, bLm, lowerPathW);
    }

    // ---- lado A: o register.cpp da Steinberg ----
    LONG rcSdk = -1;
    if (!overrideTo(aCr, aLm)) {
        fail(&g_returnChecks, name, "RegOverridePredefKey falhou para o lado do SDK");
    } else {
        rcSdk = run(true, modulePathW, modulePathA);
        overrideOff();
    }

    // ---- lado B: o nosso asioreg.cpp ----
    LONG rcOurs = -1;
    if (!overrideTo(bCr, bLm)) {
        fail(&g_returnChecks, name, "RegOverridePredefKey falhou para o nosso lado");
    } else {
        rcOurs = run(false, modulePathW, modulePathA);
        overrideOff();
    }

    char label[256];
    _snprintf(label, 255, "%s: as duas funcoes devolveram sucesso", name);
    label[255] = 0;
    if (rcSdk == 0 && rcOurs == bcd::kRegOk) {
        pass(&g_returnChecks, label);
    } else {
        char detail[160];
        _snprintf(detail, 159, "SDK devolveu %ld, o nosso devolveu %ld", rcSdk, rcOurs);
        detail[159] = 0;
        fail(&g_returnChecks, label, detail);
    }

    // ---- comparacao ----
    static Dump da;
    static Dump db;
    dumpInit(&da);
    dumpInit(&db);
    dumpKey(&da, aCr, 0, L"HKCR");
    dumpKey(&da, aLm, 0, L"HKLM");
    dumpKey(&db, bCr, 0, L"HKCR");
    dumpKey(&db, bLm, 0, L"HKLM");

    RegCloseKey(aCr); RegCloseKey(aLm);
    RegCloseKey(bCr); RegCloseKey(bLm);

    _snprintf(label, 255, "%s: as duas arvores sao identicas", name);
    label[255] = 0;

    if (da.overflow || db.overflow) {
        fail(&g_treeChecks, label, "o despejo nao caberia no buffer - aumente kDumpMax");
    } else if (strcmp(da.text, db.text) == 0) {
        pass(&g_treeChecks, label);
        if (g_verbose)
            printf("%s", da.text);
    } else {
        char detail[1200];
        firstDifference(da.text, db.text, detail, sizeof(detail));
        fail(&g_treeChecks, label, detail);
        printf("--- arvore do SDK ---\n%s--- a nossa ---\n%s", da.text, db.text);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// O que cada cenario faz
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// szdllname = 0 de proposito no lado do SDK: o GetModuleHandle(NULL) dele devolve o
// handle do PROPRIO regcheck.exe, e e o caminho dele que vai para o registro. O nosso lado
// chega ao mesmo caminho por thisModulePath(), e a igualdade dos dois e conferida em main()
// antes de qualquer cenario rodar - sem ela, os cenarios comparariam caminhos diferentes e
// falhariam por um motivo que nao e o que se quer medir.
static LONG runRegister(bool sdkSide, const wchar_t* modulePathW, char* modulePathA)
{
    (void)modulePathA;
    if (sdkSide)
        return RegisterAsioDriver(kClsid, 0, kRegNameA, kDescA, kModelA);
    return bcd::registerAsioDriver(kClsid, modulePathW, kRegNameW, kDescW, kModelW);
}

static LONG runRegisterTwice(bool sdkSide, const wchar_t* modulePathW, char* modulePathA)
{
    const LONG first = runRegister(sdkSide, modulePathW, modulePathA);
    if (first != 0)
        return first;
    return runRegister(sdkSide, modulePathW, modulePathA);
}

static LONG runUnregister(bool sdkSide, const wchar_t* modulePathW, char* modulePathA)
{
    (void)modulePathW;
    (void)modulePathA;
    if (sdkSide)
        return UnregisterAsioDriver(kClsid, 0, kRegNameA);
    return bcd::unregisterAsioDriver(kClsid, kRegNameW);
}

static LONG runRegisterThenUnregister(bool sdkSide, const wchar_t* modulePathW,
                                      char* modulePathA)
{
    const LONG first = runRegister(sdkSide, modulePathW, modulePathA);
    if (first != 0)
        return first;
    return runUnregister(sdkSide, modulePathW, modulePathA);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Semeaduras
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

static void seedClsidElsewhere(HKEY hkcr, HKEY hklm, const wchar_t* lowerPathW)
{
    (void)hklm;
    (void)lowerPathW;
    // Uma instalacao anterior em OUTRA pasta, com descricao velha e uma subchave que nao
    // deveria estar la. Exercita o ramo "o caminho mudou": a subarvore inteira tem de
    // sair antes de ser reescrita.
    wchar_t clsidKey[512];
    _snwprintf(clsidKey, 511, L"CLSID\\%ls", kClsidText);
    clsidKey[511] = 0;
    setStringAt(hkcr, clsidKey, 0, L"Descricao de uma instalacao anterior");

    wchar_t inproc[512];
    _snwprintf(inproc, 511, L"%ls\\InprocServer32", clsidKey);
    inproc[511] = 0;
    setStringAt(hkcr, inproc, 0, L"c:\\outra\\pasta\\bcdasio.dll");
    setStringAt(hkcr, inproc, L"ThreadingModel", L"Both");

    wchar_t junk[512];
    _snwprintf(junk, 511, L"%ls\\SujeiraAntiga", clsidKey);
    junk[511] = 0;
    setStringAt(hkcr, junk, L"deveria", L"sair junto");
}

static void seedClsidSamePath(HKEY hkcr, HKEY hklm, const wchar_t* lowerPathW)
{
    (void)hklm;
    // O MESMO arquivo ja registrado, com descricao diferente, ThreadingModel diferente e
    // uma subchave estranha. Exercita o ramo oposto: nada disso pode ser tocado. E o ramo
    // que decide se um segundo `regsvr32` refresca os valores ou nao, e a resposta nao e
    // obvia - so o comportamento medido decide.
    wchar_t clsidKey[512];
    _snwprintf(clsidKey, 511, L"CLSID\\%ls", kClsidText);
    clsidKey[511] = 0;
    setStringAt(hkcr, clsidKey, 0, L"Descricao que NAO pode ser refrescada");

    wchar_t inproc[512];
    _snwprintf(inproc, 511, L"%ls\\InprocServer32", clsidKey);
    inproc[511] = 0;
    setStringAt(hkcr, inproc, 0, lowerPathW);
    setStringAt(hkcr, inproc, L"ThreadingModel", L"Both");

    wchar_t junk[512];
    _snwprintf(junk, 511, L"%ls\\SubchaveDeOutroPrograma", clsidKey);
    junk[511] = 0;
    setStringAt(hkcr, junk, L"tem", L"de sobreviver");
}

static void seedOtherAsioDriver(HKEY hkcr, HKEY hklm, const wchar_t* lowerPathW)
{
    (void)hkcr;
    (void)lowerPathW;
    // O "Realtek ASIO" que existe de verdade na maquina do dono. Se o registro apagasse a
    // chave pai SOFTWARE\ASIO em vez de so a subchave dele, o driver de audio da
    // placa-mae sairia da lista - e o dono descobriria isso depois, sem pista nenhuma.
    setStringAt(hklm, L"SOFTWARE\\ASIO\\Realtek ASIO", L"CLSID",
                L"{A80362FF-CE76-4DD9-874A-704C57BF0D6A}");
    setStringAt(hklm, L"SOFTWARE\\ASIO\\Realtek ASIO", L"Description", L"Realtek ASIO");
}

static void seedOurAsioEntryDirty(HKEY hkcr, HKEY hklm, const wchar_t* lowerPathW)
{
    (void)hkcr;
    (void)lowerPathW;
    // A nossa propria subchave, de uma versao anterior, com valor e subchave a mais.
    wchar_t path[512];
    _snwprintf(path, 511, L"SOFTWARE\\ASIO\\%ls", kRegNameW);
    path[511] = 0;
    setStringAt(hklm, path, L"CLSID", L"{00000000-0000-0000-0000-000000000000}");
    setStringAt(hklm, path, L"Description", L"Versao anterior");
    setStringAt(hklm, path, L"ValorQueSaiu", L"de uma versao antiga");

    wchar_t sub[512];
    _snwprintf(sub, 511, L"%ls\\SubchaveQueSaiu", path);
    sub[511] = 0;
    setStringAt(hklm, sub, L"tambem", L"deveria sair");
}

static void seedOtherPlusOurs(HKEY hkcr, HKEY hklm, const wchar_t* lowerPathW)
{
    seedOtherAsioDriver(hkcr, hklm, lowerPathW);
    seedOurAsioEntryDirty(hkcr, hklm, lowerPathW);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// O registro DE VERDADE da maquina: despejado antes e depois de tudo.
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
static void dumpRealRegistry(Dump* d)
{
    dumpInit(d);
    wchar_t clsidKey[512];
    _snwprintf(clsidKey, 511, L"CLSID\\%ls", kClsidText);
    clsidKey[511] = 0;
    dumpKey(d, HKEY_CLASSES_ROOT, clsidKey, L"HKCR\\CLSID\\{...}");
    dumpKey(d, HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", L"HKLM\\SOFTWARE\\ASIO");
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0)
            g_verbose = true;

    printf("== regcheck: asioreg.cpp (nosso) contra o register.cpp do ASIO SDK ==\n");
    printf("   Ferramenta de verificacao LOCAL. Nao entra no BcdAsio.dll.\n");
    printf("   NAO escreve no registro de verdade: HKCR e HKLM sao desviados, dentro\n");
    printf("   deste processo so, para HKCU\\%ls.\n", kTestRoot);
    printf("   Rode com -v para ver os despejos.\n");

    // ---- o registro de verdade, ANTES ----
    static Dump realBefore;
    dumpRealRegistry(&realBefore);

    // ---- o caminho do modulo: os dois lados tem de chegar ao MESMO ----
    wchar_t modulePathW[bcd::kRegPathMax];
    if (!bcd::thisModulePath(modulePathW, bcd::kRegPathMax)) {
        printf("\n  FALHA   thisModulePath() nao devolveu o caminho do regcheck.exe\n");
        printf("\nREGCHECK_FAIL\n");
        return 1;
    }

    char sdkPathA[bcd::kRegPathMax];
    sdkPathA[0] = 0;
    GetModuleFileNameA(GetModuleHandleA(0), sdkPathA, bcd::kRegPathMax);

    wchar_t sdkPathW[bcd::kRegPathMax];
    MultiByteToWideChar(CP_ACP, 0, sdkPathA, -1, sdkPathW, bcd::kRegPathMax);

    if (_wcsicmp(modulePathW, sdkPathW) == 0) {
        pass(&g_safetyChecks, "os dois lados registram o MESMO caminho de modulo");
    } else {
        fail(&g_safetyChecks, "os dois lados registram o MESMO caminho de modulo",
             "thisModulePath() e o GetModuleHandle(NULL) do SDK discordam - os cenarios "
             "abaixo compararariam caminhos diferentes");
    }

    // O caminho em minusculas, para a semeadura do cenario "mesmo caminho ja registrado".
    wchar_t lowerPathW[bcd::kRegPathMax];
    wcscpy(lowerPathW, modulePathW);
    CharLowerW(lowerPathW);

    printf("\n   modulo registrado nos cenarios: %ls\n", lowerPathW);

    // ---- os cenarios ----
    runScenario("registro numa arvore limpa",
                0, runRegister, modulePathW, lowerPathW, sdkPathA);

    runScenario("registro repetido, mesmo caminho (idempotencia)",
                0, runRegisterTwice, modulePathW, lowerPathW, sdkPathA);

    runScenario("registro com o CLSID apontando para OUTRA pasta",
                seedClsidElsewhere, runRegister, modulePathW, lowerPathW, sdkPathA);

    runScenario("registro com o CLSID ja apontando para ESTE arquivo",
                seedClsidSamePath, runRegister, modulePathW, lowerPathW, sdkPathA);

    runScenario("registro com outro driver ASIO ja na lista",
                seedOtherAsioDriver, runRegister, modulePathW, lowerPathW, sdkPathA);

    runScenario("registro sobre a nossa propria entrada suja",
                seedOurAsioEntryDirty, runRegister, modulePathW, lowerPathW, sdkPathA);

    runScenario("desregistro depois de registrar",
                0, runRegisterThenUnregister, modulePathW, lowerPathW, sdkPathA);

    runScenario("desregistro numa arvore limpa (nada para apagar)",
                0, runUnregister, modulePathW, lowerPathW, sdkPathA);

    runScenario("desregistro com outro driver ASIO na lista",
                seedOtherPlusOurs, runRegisterThenUnregister,
                modulePathW, lowerPathW, sdkPathA);

    // ---- limpeza, e o registro de verdade DEPOIS ----
    deleteTestRoot();

    HKEY leftover;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kTestRoot, 0, KEY_READ,
                      &leftover) == ERROR_SUCCESS) {
        RegCloseKey(leftover);
        fail(&g_safetyChecks, "as subarvores de teste em HKCU foram removidas",
             "HKCU\\Software\\BcdAsioRegCheck continua no registro");
    } else {
        pass(&g_safetyChecks, "as subarvores de teste em HKCU foram removidas");
    }

    static Dump realAfter;
    dumpRealRegistry(&realAfter);
    if (strcmp(realBefore.text, realAfter.text) == 0) {
        pass(&g_safetyChecks, "o registro DE VERDADE do driver esta intacto");
        if (g_verbose)
            printf("--- registro de verdade, inalterado ---\n%s", realAfter.text);
    } else {
        char detail[1200];
        firstDifference(realBefore.text, realAfter.text, detail, sizeof(detail));
        fail(&g_safetyChecks, "o registro DE VERDADE do driver esta intacto", detail);
        printf("--- antes ---\n%s--- depois ---\n%s", realBefore.text, realAfter.text);
    }

    const int total = g_returnChecks + g_treeChecks + g_safetyChecks;
    printf("\n== %d verificacoes (%d de retorno, %d de arvore identica, %d de "
           "seguranca), %d falhas ==\n",
           total, g_returnChecks, g_treeChecks, g_safetyChecks, g_fail);
    printf("== %d cenarios de registro/desregistro ==\n", g_scenarios);

    if (g_fail) {
        printf("\nREGCHECK_FAIL: o nosso registro NAO produz o mesmo estado que o do SDK. "
               "NAO troque o driver para ele.\n");
        return 1;
    }
    printf("\nREGCHECK_OK\n");
    return 0;
}
