// Ponto de entrada do DLL.
//
// ESTE ARQUIVO FOI APROVEITADO, E NAO SUBSTITUIDO. Ele ja era nosso - existia porque o
// CRT chama `DllMain` e o encanamento do SDK exportava o ponto de entrada com outro nome
// (`DllEntryPoint`, em dllentry.cpp), entao este arquivo era a ponte entre os dois. Com o
// dllentry.cpp fora do produto, a ponte deixou de ter para onde apontar e o corpo virou o
// ponto de entrada de verdade. Duas linhas de cada lado; nada aqui merecia arquivo novo.
//
// O QUE ESTE PONTO DE ENTRADA FAZ, E O QUE ELE DELIBERADAMENTE NAO FAZ MAIS
// O DllEntryPoint do SDK fazia mais tres coisas no attach, e nenhuma delas sobreviveu ao
// exame:
//
//   * `DbgInitialise` / `DbgTerminate` - inicializacao do sistema de log de depuracao da
//     Microsoft, que neste projeto compila para nada (DEBUG nao esta definido) e cujo
//     unico efeito era existir;
//   * `GetVersionEx` para descobrir se a maquina era Windows 95 ou NT, guardado em
//     `g_amPlatform` e `g_osInfo` para "decidir se ha suporte a Unicode". A resposta em
//     2026 e sim, e o `GetVersionEx` esta OBSOLETO desde o Windows 8.1 - ele passou a
//     mentir a versao por padrao. Codigo que le uma resposta errada para decidir algo que
//     ninguem mais pergunta;
//   * `DllInitClasses`, que percorria a tabela `CFactoryTemplate` chamando a rotina de
//     inicializacao de cada classe. Este DLL tem uma classe e ela nao tem rotina de
//     inicializacao: o laco era sobre um vetor de um elemento com um ponteiro nulo dentro.
//
// NO DETACH NAO SE FAZ NADA, e isto e decisao registrada, nao esquecimento. Ver a nota
// sobre ordem de destruicao no fim de comserver.cpp: destruir estado global aqui e o
// mesmo defeito que o motor de audio e a casca ASIO ja resolveram vazando de proposito -
// no DLL_PROCESS_DETACH os threads do processo ja morreram em ordem arbitraria (ou, no
// caso de um FreeLibrary, ainda estao vivos), e um objeto destruido neste instante e um
// travamento dentro do processo do software de DJ.

#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH) {
        // Dispensa as notificacoes de criacao e morte de thread. Isto e o que o
        // encanamento do SDK ja fazia, e vale MAIS aqui do que valia la: o software de DJ
        // cria e destroi threads a vontade, e cada notificacao dessas toma o lock do
        // carregador do Windows. Nao esta no caminho de audio (nao se criam threads por
        // bloco), mas e custo sem nenhum uso - este DLL nao tem nada para fazer quando um
        // thread do host nasce.
        //
        // A ressalva conhecida e o CRT ESTATICO (o /LD do build.bat implica /MT), que
        // historicamente usava DLL_THREAD_DETACH para limpar dados por thread. O CRT atual
        // faz essa limpeza por callback de FLS, que o DisableThreadLibraryCalls nao
        // desliga - e por isso a chamada continua correta. Se algum dia o driver ganhar
        // `__declspec(thread)` proprio, e esta linha que tem de ser reexaminada.
        DisableThreadLibraryCalls(hInstance);
    }

    return TRUE;
}
