#pragma once

#include <windows.h>

// Registro do driver ASIO no Windows, escrito para este projeto.
//
// SUBSTITUI `native/ASIOSDK/common/register.cpp`, que esta sob a licenca dupla da
// Steinberg (proprietaria ou GPL v3) e por isso nao pode entrar num produto que o dono
// queira publicar com licenca propria.
//
// QUAL E A ESPECIFICACAO DESTE ARQUIVO
// Nao e o codigo que saiu, e nao e a documentacao da Steinberg: e O REGISTRO QUE
// FUNCIONA NA MAQUINA DO DONO, lido antes de escrever uma linha. Sao cinco valores, em
// duas arvores, e nada mais:
//
//   HKEY_CLASSES_ROOT\CLSID\{B0D3000A-51E7-4C2B-9F3A-1234ABCD5678}
//       (padrao)          REG_SZ  "Behringer BCD3000 ASIO Driver"
//   HKEY_CLASSES_ROOT\CLSID\{B0D3000A-...}\InprocServer32
//       (padrao)          REG_SZ  "<caminho do DLL, EM MINUSCULAS>"
//       ThreadingModel    REG_SZ  "Apartment"
//   HKEY_LOCAL_MACHINE\SOFTWARE\ASIO\Behringer BCD3000
//       CLSID             REG_SZ  "{B0D3000A-51E7-4C2B-9F3A-1234ABCD5678}"
//       Description       REG_SZ  "Behringer BCD3000 ASIO Driver"
//
// Tres detalhes desse estado foram MEDIDOS, e nao presumidos, porque cada um deles e
// uma forma de o registro sair "quase certo":
//
//  1. O CAMINHO VAI EM MINUSCULAS. O register.cpp do SDK passava o caminho por
//     `CharLower` antes de gravar, e o registro desta maquina esta assim. Nao e escolha
//     de estilo: e o que ha no disco, e a etapa que trocou o codigo tinha de produzir o
//     MESMO valor byte a byte. Quem le o valor de volta compara sem diferenciar caixa
//     (o proprio instalador usa `_wcsicmp`), entao a caixa nao muda comportamento -
//     mas mudar o valor mudaria a evidencia.
//
//  2. OS VALORES TERMINAM COM NUL. O `createRegStringValue` do SDK passava
//     `strlen(valor)` como tamanho, ou seja SEM o terminador. Ainda assim os valores
//     desta maquina tem o terminador: medido com RegQueryValueEx, o valor padrao do
//     CLSID tem cbData = 60 para uma cadeia de 29 caracteres, isto e 30 unidades de 16
//     bits.
//
//     QUEM ACRESCENTA O TERMINADOR E O `advapi32`, NOS DOIS CAMINHOS - e este item
//     afirmava o contrario. A versao anterior dizia que o responsavel era o involucro
//     ANSI do kernel, isto e, que o terminador aparecia porque o RegSetValueExA converte
//     para UTF-16 e termina de passagem. A sonda escrita no MESMO commit que este arquivo
//     (Step 8 da Tarefa 12) mediu e desmentiu: pelo caminho *** Unicode ***, onde nao ha
//     conversao nenhuma, `cbData = 58` volta 60 e `cbData = 52` volta 52. Ou seja, o que
//     decide nao e ANSI contra Unicode: e o tamanho informado ser EXATAMENTE o
//     comprimento da cadeia, e nesse caso o advapi32 acrescenta o terminador nos dois
//     caminhos.
//     Isso importa porque o topo deste arquivo e tratado neste projeto como a
//     ESPECIFICACAO do registro do driver: quem o ler com o motivo errado decide com um
//     modelo errado - e foi o mesmo motivo errado que ficou escrito em
//     `installer/common.cpp` (corrigido junto).
//     Nada disso muda o que ESTE arquivo faz: aqui o tamanho INCLUI o terminador
//     explicitamente, o que da o mesmo cbData e nao depende de comportamento nenhum do
//     advapi32.
//
//  3. A CHAVE `SOFTWARE\ASIO` E COMPARTILHADA. Nesta maquina ela tem tambem o
//     "Realtek ASIO". Ela e criada se faltar e NUNCA e apagada - so a subchave deste
//     driver sai no desregistro. Apagar a chave pai desregistraria o driver de audio
//     da placa-mae do dono.
//
// DUAS MELHORIAS DELIBERADAS SOBRE O CODIGO QUE SAIU, e nenhuma delas muda o resultado
// no registro:
//
//  a. O CAMINHO DO MODULO E PARAMETRO. O register.cpp descobria o proprio caminho com
//     `GetModuleHandle("BcdAsio.dll")` - por NOME. Renomear o arquivo fazia o registro
//     falhar com "modulo nao encontrado", e o codigo nao tinha como saber que o nome
//     estava certo. Aqui quem chama passa o caminho, e `thisModulePath` o descobre por
//     ENDERECO (ver o comentario da funcao). Como efeito colateral util, isto e o que
//     torna `regcheck.cpp` possivel: o arnes consegue pedir aos dois lados que
//     registrem o MESMO caminho e comparar o resultado.
//
//  b. NADA E VAZADO. O register.cpp chamava `StringFromCLSID`, que aloca com
//     `CoTaskMemAlloc`, e nunca chamava `CoTaskMemFree` - um vazamento por chamada de
//     registro, pequeno e real. Aqui a conversao e `StringFromGUID2`, que escreve num
//     buffer do chamador e nao aloca nada.

namespace bcd {

// Codigos de retorno. Distintos entre si de proposito: o `DllRegisterServer` mostra o
// numero ao usuario numa caixa de mensagem, e "falhou" sem dizer ONDE nao ajuda ninguem
// a consertar. A faixa -101 em diante e a mesma que o codigo antigo usava, para que
// qualquer anotacao antiga do dono continue fazendo sentido.
enum {
    kRegOk             = 0,
    kRegErrModule      = -101,  // nao foi possivel achar o proprio modulo
    kRegErrModulePath  = -102,  // achou o modulo mas nao o caminho dele
    kRegErrClsidText   = -103,  // o GUID nao virou texto
    kRegErrPathTooLong = -104,  // o caminho do DLL nao cabe em kRegPathMax
    kRegErrCreateKey   = -105,  // nao foi possivel criar uma chave
    kRegErrSetValue    = -106   // nao foi possivel gravar um valor
};

enum {
    // Um caminho de arquivo do Windows pode passar de MAX_PATH. 1024 nao cobre o
    // extremo teorico (32767), e nao tenta: a alternativa seria alocacao dinamica num
    // caminho de codigo que roda uma vez por instalacao, e o modo de falha aqui e
    // EXPLICITO - kRegErrPathTooLong - em vez de um caminho truncado no registro, que e
    // o que o codigo antigo produzia calado com o buffer de 360 bytes dele.
    kRegPathMax  = 1024,

    // "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" tem 38 caracteres; com o terminador, 39.
    kGuidTextMax = 39
};

// Caminho completo do modulo (DLL ou EXE) que contem o codigo desta funcao.
//
// Devolve false, sem tocar em `out` alem de zera-lo, se o modulo nao for encontrado ou
// se o caminho nao couber em `cch`.
bool thisModulePath(wchar_t* out, size_t cch);

// Escreve as cinco entradas descritas no topo deste arquivo.
//
// `modulePath` e gravado EM MINUSCULAS (ver o item 1 acima); quem chama passa o caminho
// como o sistema o devolveu.
//
// Devolve kRegOk, ou um dos codigos acima.
LONG registerAsioDriver(const CLSID& clsid, const wchar_t* modulePath,
                        const wchar_t* regName, const wchar_t* description,
                        const wchar_t* threadingModel);

// Remove a subarvore do CLSID em HKEY_CLASSES_ROOT e a subchave `regName` de
// HKEY_LOCAL_MACHINE\SOFTWARE\ASIO. A chave `SOFTWARE\ASIO` em si nao e tocada.
//
// Nao precisa do caminho do modulo, ao contrario da funcao equivalente do SDK, que
// exigia achar o proprio DLL por nome e RECUSAVA desregistrar quando nao conseguia -
// um modo de falha que nao protegia nada, porque o caminho que ela obtinha era
// calculado e jogado fora sem ser usado.
//
// Devolve kRegOk, ou kRegErrClsidText.
LONG unregisterAsioDriver(const CLSID& clsid, const wchar_t* regName);

}
