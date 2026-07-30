#include "asioreg.h"

#include <objbase.h>    // StringFromGUID2
#include <wchar.h>

namespace {

// Os nomes de chave e de valor, num lugar so. Nao e mania de constante: cada um destes
// textos e um contrato com o registro da maquina do dono, e um deles escrito duas vezes
// em dois lugares e a forma mais barata de eles divergirem.
const wchar_t* const kClsidRootKey  = L"CLSID";
const wchar_t* const kInprocKeyName = L"InprocServer32";
const wchar_t* const kThreadModelValue = L"ThreadingModel";
const wchar_t* const kAsioRootKey   = L"SOFTWARE\\ASIO";
const wchar_t* const kClsidValueName = L"CLSID";
const wchar_t* const kDescValueName  = L"Description";

bool keyExists(HKEY root, const wchar_t* path)
{
    HKEY key;
    if (RegOpenKeyExW(root, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

// Cria a chave e as intermediarias que faltarem. Devolve ERROR_SUCCESS tambem quando a
// chave JA existia - que e o caso normal de um segundo registro, e nao um erro.
LONG createKey(HKEY root, const wchar_t* path)
{
    HKEY key;
    const LONG rc = RegCreateKeyExW(root, path, 0, 0, 0, KEY_WRITE, 0, &key, 0);
    if (rc != ERROR_SUCCESS)
        return rc;
    RegCloseKey(key);
    return ERROR_SUCCESS;
}

// Grava um REG_SZ. `name` nulo grava o valor PADRAO (o que o regedit mostra como
// "(Padrao)" e o reg export escreve como `@=`), que e onde tres das cinco entradas
// deste driver vivem.
//
// O tamanho INCLUI o terminador. Ver o item 2 do topo de asioreg.h: e o que reproduz o
// cbData dos valores que ja estao na maquina, e e tambem a unica forma de um leitor que
// use RegQueryValueEx receber uma cadeia terminada.
LONG setString(HKEY root, const wchar_t* path, const wchar_t* name,
               const wchar_t* value)
{
    HKEY key;
    LONG rc = RegOpenKeyExW(root, path, 0, KEY_SET_VALUE, &key);
    if (rc != ERROR_SUCCESS)
        return rc;
    const DWORD bytes = (DWORD)((wcslen(value) + 1) * sizeof(wchar_t));
    rc = RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)value, bytes);
    RegCloseKey(key);
    return rc;
}

// Le um REG_SZ para `out`, sempre terminado. Devolve false se a chave, o valor ou o
// tipo nao servirem - e nesse caso `out` fica com cadeia vazia, nunca com lixo de
// pilha (que era o que o getRegString do SDK deixava quando o valor gravado nao tinha
// terminador).
bool getString(HKEY root, const wchar_t* path, const wchar_t* name,
               wchar_t* out, size_t cch)
{
    if (!out || cch == 0)
        return false;
    out[0] = 0;

    HKEY key;
    if (RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;

    DWORD type  = 0;
    DWORD bytes = (DWORD)((cch - 1) * sizeof(wchar_t));
    const LONG rc = RegQueryValueExW(key, name, 0, &type, (LPBYTE)out, &bytes);
    RegCloseKey(key);

    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        out[0] = 0;
        return false;
    }
    // Terminar pelo tamanho que a chamada relatou, e nao confiar no conteudo: um valor
    // gravado sem terminador volta sem terminador.
    out[bytes / sizeof(wchar_t)] = 0;
    out[cch - 1] = 0;
    return true;
}

// Apaga a chave `name` sob `parentPath`, com tudo o que houver dentro dela.
//
// O acesso pedido no pai e o minimo que o RegDeleteTree exige, e nao KEY_ALL_ACCESS: um
// registro que falha por falta de permissao e um problema; um que apaga mais do que
// devia por ter pedido permissao demais e outro.
LONG deleteTree(HKEY root, const wchar_t* parentPath, const wchar_t* name)
{
    HKEY parent;
    LONG rc = RegOpenKeyExW(root, parentPath, 0,
                            DELETE | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE |
                            KEY_SET_VALUE, &parent);
    if (rc != ERROR_SUCCESS)
        return rc;
    rc = RegDeleteTreeW(parent, name);
    RegCloseKey(parent);
    return rc;
}

// Copia `in` para `out` em minusculas. Devolve false se nao couber.
bool copyLower(wchar_t* out, size_t cch, const wchar_t* in)
{
    const size_t n = wcslen(in);
    if (n + 1 > cch)
        return false;
    memcpy(out, in, (n + 1) * sizeof(wchar_t));
    // CharLowerW e nao _wcslwr: e a mesma funcao que o codigo antigo usava (na variante
    // ANSI dele), e ela respeita a localidade do sistema. Para o caminho desta maquina,
    // que e ASCII, as duas dao o mesmo resultado - e regcheck.cpp confere a igualdade
    // com o caminho de verdade, nao com um caso inventado.
    CharLowerW(out);
    return true;
}

// Monta "CLSID\{...}" e "CLSID\{...}\InprocServer32". Os dois cabem folgados em
// kRegPathMax: 5 + 1 + 38 + 1 + 14 = 59 caracteres no maior deles.
void buildClsidKeys(const wchar_t* clsidText, wchar_t* clsidKey, wchar_t* inprocKey)
{
    _snwprintf(clsidKey, bcd::kRegPathMax - 1, L"%s\\%s", kClsidRootKey, clsidText);
    clsidKey[bcd::kRegPathMax - 1] = 0;
    _snwprintf(inprocKey, bcd::kRegPathMax - 1, L"%s\\%s", clsidKey, kInprocKeyName);
    inprocKey[bcd::kRegPathMax - 1] = 0;
}

// Ancora de endereco para o GetModuleHandleEx de thisModulePath(). Tem de ser um DADO
// deste modulo: o endereco de uma FUNCAO tambem serviria para o Windows, mas a conversao
// de ponteiro de funcao para ponteiro de dados e um aviso do compilador (C4054) e este
// projeto compila com /W4 /WX.
char g_moduleAnchor = 0;

}

bool bcd::thisModulePath(wchar_t* out, size_t cch)
{
    if (!out || cch == 0)
        return false;
    out[0] = 0;

    HMODULE mod = 0;

    // GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS descobre o modulo pelo ENDERECO de algo que
    // mora dentro dele. Isto substitui o `GetModuleHandle("BcdAsio.dll")` do codigo que
    // saiu, que procurava pelo NOME do arquivo: renomear o DLL fazia o registro falhar,
    // e um DLL nao tem por que saber com que nome foi gravado no disco.
    //
    // GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT nao e opcional. Sem ele esta chamada
    // INCREMENTA a contagem de carga do proprio modulo, e nada a devolveria: o
    // DllCanUnloadNow poderia responder S_OK para sempre e o Windows nunca descarregaria
    // o DLL - com a agravante de que o sintoma so apareceria depois de um registro, que
    // e um caminho que quase ninguem exercita duas vezes.
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&g_moduleAnchor), &mod))
        return false;

    const DWORD n = GetModuleFileNameW(mod, out, (DWORD)cch);
    // n == 0 e erro; n == cch e TRUNCAMENTO (a funcao preenche o buffer inteiro e
    // termina, sem dizer nada no valor de retorno em versoes antigas). Nos dois casos a
    // resposta e "nao sei o caminho", e nao um caminho pela metade indo para o registro.
    if (n == 0 || n >= cch) {
        out[0] = 0;
        return false;
    }
    return true;
}

LONG bcd::registerAsioDriver(const CLSID& clsid, const wchar_t* modulePath,
                             const wchar_t* regName, const wchar_t* description,
                             const wchar_t* threadingModel)
{
    wchar_t clsidText[kGuidTextMax];
    if (StringFromGUID2(clsid, clsidText, kGuidTextMax) == 0)
        return kRegErrClsidText;

    wchar_t path[kRegPathMax];
    if (!copyLower(path, kRegPathMax, modulePath))
        return kRegErrPathTooLong;

    wchar_t clsidKey[kRegPathMax];
    wchar_t inprocKey[kRegPathMax];
    buildClsidKeys(clsidText, clsidKey, inprocKey);

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // HKEY_CLASSES_ROOT\CLSID\{...}
    //
    // A DECISAO de reescrever ou nao e copiada do comportamento que esta na maquina, e
    // ela nao e obvia: quando a chave JA existe e o InprocServer32 dela JA aponta para
    // este mesmo arquivo, nada e tocado. Consequencia visivel: um registro repetido nao
    // "refresca" a Description. Consequencia util: qualquer subchave que outro programa
    // tenha posto ali sobrevive.
    //
    // Quando o caminho DIFERE, a subarvore inteira sai antes de ser reescrita, e isto e
    // o que limpa sujeira de uma instalacao anterior em outra pasta em vez de deixa-la
    // conviver com os valores novos.
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    bool writeClsid = true;
    if (keyExists(HKEY_CLASSES_ROOT, clsidKey)) {
        wchar_t current[kRegPathMax];
        wchar_t currentLower[kRegPathMax];
        currentLower[0] = 0;
        if (getString(HKEY_CLASSES_ROOT, inprocKey, 0, current, kRegPathMax))
            copyLower(currentLower, kRegPathMax, current);

        if (wcscmp(currentLower, path) == 0) {
            writeClsid = false;
        } else {
            // Falhar aqui NAO aborta o registro, de proposito. Os valores sao
            // reescritos logo abaixo de qualquer forma, e o resultado util - o host
            // acha o DLL certo - e o mesmo. Abortar trocaria "registro correto com
            // sujeira antiga ao lado" por "nenhum registro", que e pior.
            deleteTree(HKEY_CLASSES_ROOT, kClsidRootKey, clsidText);
        }
    }

    if (writeClsid) {
        if (createKey(HKEY_CLASSES_ROOT, clsidKey) != ERROR_SUCCESS)
            return kRegErrCreateKey;
        if (setString(HKEY_CLASSES_ROOT, clsidKey, 0, description) != ERROR_SUCCESS)
            return kRegErrSetValue;
        if (createKey(HKEY_CLASSES_ROOT, inprocKey) != ERROR_SUCCESS)
            return kRegErrCreateKey;
        if (setString(HKEY_CLASSES_ROOT, inprocKey, 0, path) != ERROR_SUCCESS)
            return kRegErrSetValue;
        if (setString(HKEY_CLASSES_ROOT, inprocKey, kThreadModelValue,
                      threadingModel) != ERROR_SUCCESS)
            return kRegErrSetValue;
    }

    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    // HKEY_LOCAL_MACHINE\SOFTWARE\ASIO\<regName>
    //
    // E aqui que os softwares de DJ acham a LISTA de drivers ASIO da maquina: eles
    // enumeram as subchaves desta chave e leem o CLSID de cada uma.
    // - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
    wchar_t nameKey[kRegPathMax];
    _snwprintf(nameKey, kRegPathMax - 1, L"%s\\%s", kAsioRootKey, regName);
    nameKey[kRegPathMax - 1] = 0;

    // A subchave deste driver sai INTEIRA antes de ser recriada, e nao e redundante com
    // as escritas abaixo: e o que remove valor ou subchave extra de uma versao anterior.
    // A chave PAI (SOFTWARE\ASIO) nunca e apagada - nesta maquina ela tem tambem o
    // "Realtek ASIO", e apaga-la desregistraria o audio da placa-mae do dono.
    if (keyExists(HKEY_LOCAL_MACHINE, nameKey))
        deleteTree(HKEY_LOCAL_MACHINE, kAsioRootKey, regName);

    // Cria SOFTWARE\ASIO tambem, se faltar - e o caso de uma maquina sem nenhum outro
    // driver ASIO instalado.
    if (createKey(HKEY_LOCAL_MACHINE, nameKey) != ERROR_SUCCESS)
        return kRegErrCreateKey;
    if (setString(HKEY_LOCAL_MACHINE, nameKey, kClsidValueName,
                  clsidText) != ERROR_SUCCESS)
        return kRegErrSetValue;
    if (setString(HKEY_LOCAL_MACHINE, nameKey, kDescValueName,
                  description) != ERROR_SUCCESS)
        return kRegErrSetValue;

    return kRegOk;
}

LONG bcd::unregisterAsioDriver(const CLSID& clsid, const wchar_t* regName)
{
    wchar_t clsidText[kGuidTextMax];
    if (StringFromGUID2(clsid, clsidText, kGuidTextMax) == 0)
        return kRegErrClsidText;

    wchar_t clsidKey[kRegPathMax];
    wchar_t inprocKey[kRegPathMax];
    buildClsidKeys(clsidText, clsidKey, inprocKey);
    (void)inprocKey;    // montado junto; o desregistro apaga a subarvore inteira

    if (keyExists(HKEY_CLASSES_ROOT, clsidKey))
        deleteTree(HKEY_CLASSES_ROOT, kClsidRootKey, clsidText);

    wchar_t nameKey[kRegPathMax];
    _snwprintf(nameKey, kRegPathMax - 1, L"%s\\%s", kAsioRootKey, regName);
    nameKey[kRegPathMax - 1] = 0;

    if (keyExists(HKEY_LOCAL_MACHINE, nameKey))
        deleteTree(HKEY_LOCAL_MACHINE, kAsioRootKey, regName);

    // O resultado NAO depende de as chaves existirem: desregistrar duas vezes, ou
    // desregistrar o que nunca foi registrado, e sucesso. Um `regsvr32 /u` que falha
    // porque nao havia nada para apagar so faz o usuario procurar problema onde nao
    // tem.
    return kRegOk;
}
