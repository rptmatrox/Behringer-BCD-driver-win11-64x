#pragma once

#include <unknwn.h>

// Encanamento COM do driver: fabrica de classe, `DllGetClassObject`, `DllCanUnloadNow` e
// a contagem de vida do servidor.
//
// SUBSTITUI `native/ASIOSDK/common/combase.{h,cpp}` e `dllentry.cpp`, que carregam aviso
// de direito autoral da Microsoft de 1992-1996 e sao as classes-base de COM que o SDK
// distribui.
//
// POR QUE ISTO E CURTO, E O QUE SAIU COM ELE
// O combase.h resolvia um problema que este projeto nao tem: um objeto COM que precisa
// suportar AO MESMO TEMPO o IUnknown delegante (quando ele e agregado dentro de outro) e
// o nao-delegante (quando ele e o de fora). E daquilo que vinham a interface
// `INonDelegatingUnknown`, os tres metodos `NonDelegating*`, o macro `DECLARE_IUNKNOWN`,
// a classe `CBaseObject` so para contar objetos, a `CFactoryTemplate` para varias classes
// por DLL e o truque de reinterpretar `INonDelegatingUnknown*` como `IUnknown*` porque
// "as duas se comportam igual".
//
// Este DLL tem UMA classe, que NAO e agregavel e cujo unico cliente e um software de DJ
// chamando CoCreateInstance. Nada disso paga o proprio custo aqui, e duas coisas concretas
// se ganham ao escrever o encanamento direto:
//
//  1. A REGRA DE IDENTIDADE DO COM VOLTA A VALER. O `CUnknown::NonDelegatingQueryInterface`
//     do SDK respondia a IID_IUnknown com o endereco do subobjeto INonDelegatingUnknown -
//     um ponteiro DIFERENTE do que a mesma chamada devolvia para o CLSID do driver. O COM
//     exige que QueryInterface(IID_IUnknown) devolva SEMPRE o mesmo ponteiro para o mesmo
//     objeto, porque e assim que um cliente decide se dois ponteiros sao o mesmo objeto.
//     Nenhum host ASIO pede IID_IUnknown, e por isso o defeito nunca apareceu - o que o
//     torna exatamente o tipo de coisa que se conserta quando se esta reescrevendo, e nao
//     depois.
//
//  2. A CONTAGEM PASSA A INCLUIR A FABRICA. O `DllCanUnloadNow` do SDK olhava as travas de
//     `LockServer` e os objetos vivos, e NAO a fabrica de classe entregue ao host. Um host
//     que segurasse so a fabrica (entre o CoGetClassObject e o CreateInstance) podia ver o
//     DLL ser descarregado debaixo dela. Aqui os TRES contadores entram na resposta.
//
// O QUE NAO MUDOU, E NAO PODIA MUDAR
// O IID que o host pede ao criar o objeto e o PROPRIO CLSID do driver, e nao IID_IUnknown
// nem um IID de interface. E peculiaridade do ASIO e esta reproduzida: ver o comentario do
// `BcdAsioDriver::QueryInterface` em bcdasio.cpp, que e onde ela vive.

// O GUID desta classe. Um GUID, DOIS papeis - e por isso o nome fala de IID enquanto o
// tipo diz CLSID:
//   * como CLSID, e o que identifica a classe no registro e no CoCreateInstance;
//   * como IID, e o que o host pede como INTERFACE na mesma chamada.
// Definido em bcdasio.cpp. NAO MUDAR: quebra o registro existente na maquina do dono, e
// `installer/check.cpp` compara o texto dele com o que esta aqui.
extern const CLSID IID_ASIO_DRIVER;

namespace bcd {

// Cria uma instancia do driver e devolve nela a interface `riid`, com UMA referencia para
// quem chamou. Em qualquer caminho de erro nada vaza e `*ppv` fica nulo.
//
// Implementada em bcdasio.cpp: e o unico arquivo do projeto que conhece a classe concreta,
// e e o que deixa este arquivo sem saber nada sobre audio.
HRESULT createDriverInstance(REFIID riid, void** ppv);

// Contagem de instancias vivas do driver, para o `DllCanUnloadNow`. Chamadas pelo
// construtor e pelo destrutor do BcdAsioDriver, uma vez cada.
void comObjectCreated();
void comObjectDestroyed();

// Quantas instancias, fabricas e travas de LockServer estao de pe. Existem para o log e
// para teste; o `DllCanUnloadNow` usa os mesmos numeros.
long comObjectsAlive();
long comFactoriesAlive();
long comServerLocks();

}
