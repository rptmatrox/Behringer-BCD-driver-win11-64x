@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo VCVARS_FAIL & exit /b 1 )

set TARGET=%1
if "%TARGET%"=="" set TARGET=all

rem CFLAGS e dos ALVOS DE PRODUTO e NAO tem /I..\ASIOSDK\common - desde a etapa C2 nada
rem do SDK da Steinberg entra no BcdAsio.dll nem em nenhum executavel de teste. As duas
rem FERRAMENTAS DE VERIFICACAO que ainda precisam do SDK (abicheck e regcheck, que
rem existem justamente para comparar os dois lados) acrescentam o /I e os fontes de la
rem nas linhas de comando delas, e so la.
rem
rem Quem quiser conferir que a separacao e real: copie os fontes do produto para um
rem diretorio SEM ASIOSDK/ ao lado e rode `build.bat all` de la. E o que a verificacao
rem da Tarefa 12 faz.
set CFLAGS=/EHsc /nologo /O2 /D_CRT_SECURE_NO_WARNINGS /DWIN32 /D_WINDOWS /I.
set SDKINC=/I..\ASIOSDK\common
set CORELIBS=winusb.lib setupapi.lib ole32.lib advapi32.lib avrt.lib winmm.lib user32.lib shell32.lib

if /I "%TARGET%"=="tests"     goto :tests
if /I "%TARGET%"=="probe"     goto :probe
if /I "%TARGET%"=="testaudio" goto :testaudio
if /I "%TARGET%"=="dll"       goto :dll
if /I "%TARGET%"=="strict"    goto :strict
if /I "%TARGET%"=="abicheck"  goto :abicheck
if /I "%TARGET%"=="regcheck"  goto :regcheck
if /I "%TARGET%"=="all"       goto :all
echo Alvo desconhecido: %TARGET%  (use dll^|probe^|testaudio^|tests^|strict^|abicheck^|regcheck^|all)
exit /b 1

:all
call "%~f0" tests     || exit /b 1
call "%~f0" probe     || exit /b 1
call "%~f0" testaudio || exit /b 1
call "%~f0" dll       || exit /b 1
goto :eof

rem O veredito de cada alvo depende do codigo de saida real do cl (que engloba o
rem link, invocado por ele via /link) E TAMBEM da existencia do arquivo. So as
rem duas condicoes juntas dao BUILD_OK: o codigo de saida pega o link falho com
rem binario velho no disco; o "if not exist" pega o caso de codigo zero sem
rem arquivo produzido. Use sempre "if errorlevel 1" (avaliado na hora) e nunca
rem "%ERRORLEVEL%" dentro de um bloco ( ... ), que o cmd expande na leitura.

:tests
cl %CFLAGS% tests.cpp log.cpp format.cpp ringbuf.cpp midibridge.cpp usbdev.cpp handoff.cpp nanoclock.cpp /Fe:tests.exe /link %CORELIBS%
if errorlevel 1        ( echo ===== BUILD_FAIL: tests ===== & exit /b 1 )
if not exist tests.exe ( echo ===== BUILD_FAIL: tests ===== & exit /b 1 )
echo ===== BUILD_OK: tests.exe =====
goto :eof

:probe
cl %CFLAGS% probe.cpp usbdev.cpp log.cpp format.cpp /Fe:probe.exe /link %CORELIBS%
if errorlevel 1        ( echo ===== BUILD_FAIL: probe ===== & exit /b 1 )
if not exist probe.exe ( echo ===== BUILD_FAIL: probe ===== & exit /b 1 )
echo ===== BUILD_OK: probe.exe =====
goto :eof

:testaudio
cl %CFLAGS% testaudio.cpp audioengine.cpp usbdev.cpp ringbuf.cpp format.cpp log.cpp /Fe:testaudio.exe /link %CORELIBS%
if errorlevel 1            ( echo ===== BUILD_FAIL: testaudio ===== & exit /b 1 )
if not exist testaudio.exe ( echo ===== BUILD_FAIL: testaudio ===== & exit /b 1 )
echo ===== BUILD_OK: testaudio.exe =====
goto :eof

rem O alvo `strict` existe para tornar REPRODUZIVEL a afirmacao "limpo em /W4", que
rem ate agora dependia de alguem rodar o cl a mao com flags diferentes das do script
rem - ou seja, nao era reproduzivel por quem clonasse. Ele NAO muda nenhum dos alvos
rem acima: aqueles seguem no nivel de aviso padrao.
rem
rem Tres decisoes, e cada uma tem motivo:
rem  1. /WX junto do /W4. Sem ele o veredito dependeria de alguem LER a tela, e este
rem     script ja aprendeu essa licao da forma dificil (o BUILD_OK que saia de um
rem     `if exist` e mentia). Com /WX, um aviso novo vira BUILD_FAIL e codigo 1.
rem  2. TODOS OS NOSSOS fontes - e desde a etapa C2 isso quer dizer as 12 unidades do
rem     produto (as do alvo `dll`, la embaixo) mais os QUATRO arneses: tests.cpp,
rem     testaudio.cpp, probe.cpp e regcheck.cpp. 12 + 4 = 16, que e o numero que a linha
rem     de BUILD_OK imprime e o que a lista do :strict abaixo tem. A versao anterior desta
rem     nota dizia "os dois arneses" ao lado do 16 certo - a conta nao fechava com as
rem     palavras, e num arquivo cuja licao mais cara foi um veredito que mentia isso nao
rem     pode ficar.
rem     Antes da etapa C2 esta razao excluia os arquivos de ..\ASIOSDK\common, cujos
rem     avisos nao tinhamos como consertar e que fariam o alvo falhar sempre; agora ela e
rem     vacuamente verdadeira, porque nao existe mais fonte de terceiro para deixar de
rem     fora. Saiu `wintimer.cpp` (aposentado) e entraram `asioreg.cpp`, `comserver.cpp`,
rem     `nanoclock.cpp` e `regcheck.cpp`.
rem
rem     O `regcheck.cpp` ENTRA aqui e o `abicheck.cpp` NAO, e a diferenca nao e de
rem     categoria - os dois sao ferramenta de verificacao - e sim de include: o
rem     abicheck.cpp INCLUI os cabecalhos do SDK, e avisos deles nao temos como
rem     consertar; o regcheck.cpp nao inclui nada do SDK (ele apenas DECLARA as duas
rem     funcoes de register.cpp e as resolve no link), entao compila sozinho e nao tem
rem     por que ficar sem portao de aviso. Sem isto ele seria o unico dos nossos fontes
rem     sem nenhum.
rem  3. COMPILACAO SO (/c), com os .obj num diretorio proprio. Assim este alvo nao
rem     substitui nenhum artefato que esteja em uso; em particular NAO relinka o
rem     BcdAsio.dll, que o software de DJ pode estar segurando (LNK1104). E por ser
rem     compilacao so, o `regcheck.cpp` aqui nao precisa do register.cpp para nada -
rem     este alvo continua funcionando numa maquina sem o SDK.
:strict
if not exist strict mkdir strict
cl /c /W4 /WX %CFLAGS% /Fostrict\ ^
   audioengine.cpp bcdasio.cpp midibridge.cpp usbdev.cpp handoff.cpp ^
   ringbuf.cpp format.cpp log.cpp tests.cpp testaudio.cpp probe.cpp ^
   dllmain.cpp asioreg.cpp comserver.cpp nanoclock.cpp regcheck.cpp
if errorlevel 1 ( echo ===== BUILD_FAIL: strict ===== & exit /b 1 )
echo ===== BUILD_OK: strict (16 unidades em /W4 /WX, zero avisos) =====
goto :eof

rem O alvo `abicheck` PROVA que native/bcdasio/asioapi.h - a NOSSA declaracao da
rem interface ASIO - e binariamente identica a do SDK da Steinberg. Ele existe para que
rem a remocao da dependencia do SDK (a etapa seguinte) aconteca com prova na mao, e nao
rem com revisao de codigo: revisao nao pega ordem de metodo virtual nem deslocamento de
rem campo, e o modo de falha desses dois erros e funcionar nesta maquina e corromper na
rem de outra pessoa.
rem
rem Tres decisoes, e cada uma tem motivo:
rem  1. EXIGE o SDK no disco e diz isso em portugues quando faltar. Depois que o SDK
rem     sair do projeto, quem clonar o repositorio e rodar este alvo receberia um
rem     "arquivo nao encontrado" do compilador e nao saberia que este alvo NAO faz
rem     parte da build do produto. A mensagem existe para esse dia.
rem  2. FORA do `all`, e o abicheck.exe nao entra no BcdAsio.dll. E ferramenta de
rem     verificacao local, como o probe e o testaudio - e, ao contrario deles, tem
rem     prazo de validade: so roda enquanto o SDK estiver no disco.
rem  3. /W4 /WX aqui dentro. O alvo `strict` compila SO os nossos fontes de producao e
rem     NAO pode receber este arquivo, que inclui os cabecalhos do SDK - avisos deles
rem     nao temos como consertar, e era por isso que eles ficaram de fora de la.
rem     Compilando este alvo ja em /W4 /WX, o asioapi.h fica sob a mesma regra dos
rem     outros cabecalhos nossos sem que o alvo strict precise ser tocado.
:abicheck
if not exist ..\ASIOSDK\common\asiosys.h  goto :abicheck_nosdk
if not exist ..\ASIOSDK\common\asio.h     goto :abicheck_nosdk
if not exist ..\ASIOSDK\common\iasiodrv.h goto :abicheck_nosdk
cl /W4 /WX %CFLAGS% %SDKINC% abicheck.cpp /Fe:abicheck.exe
if errorlevel 1           ( echo ===== BUILD_FAIL: abicheck ===== & exit /b 1 )
if not exist abicheck.exe ( echo ===== BUILD_FAIL: abicheck ===== & exit /b 1 )
echo ===== BUILD_OK: abicheck.exe =====
goto :eof

:abicheck_nosdk
echo ===== BUILD_FAIL: abicheck - o ASIO SDK da Steinberg nao esta no disco =====
echo.
echo Este alvo e FERRAMENTA DE VERIFICACAO LOCAL e nao faz parte da build do produto.
echo Ele compara a NOSSA declaracao da interface ASIO ^(native/bcdasio/asioapi.h^) com
echo a do SDK, campo por campo e slot por slot, e para isso precisa dos DOIS lados.
echo.
echo Ponha o SDK em native/ASIOSDK/ ^(baixe da Steinberg; ver o README^) e rode de novo.
echo Se voce so quer compilar o driver, use os alvos dll, tests, probe ou testaudio.
exit /b 1

rem O alvo `regcheck` PROVA que native/bcdasio/asioreg.cpp - o NOSSO registro do driver -
rem escreve no registro do Windows exatamente as mesmas chaves, valores, tipos e BYTES que
rem o register.cpp da Steinberg escrevia. Ele existe pelo mesmo motivo do abicheck: a
rem etapa que trocou o codigo nao podia se apoiar em revisao, porque revisao nao ve cbData
rem nem terminador de cadeia, e o modo de falha de um registro quase certo e o driver nao
rem aparecer na lista do software de DJ sem uma linha de log em lugar nenhum.
rem
rem Ele NAO TOCA no registro de verdade da maquina: RegOverridePredefKey desvia
rem HKEY_CLASSES_ROOT e HKEY_LOCAL_MACHINE, dentro deste processo so, para duas subarvores
rem de teste em HKEY_CURRENT_USER - uma por lado -, e no fim as duas sao apagadas. Como
rem consequencia ele tambem NAO precisa de elevacao, e roda numa maquina com o driver
rem instalado e em uso sem risco nenhum.
rem
rem Tres decisoes, e cada uma tem motivo:
rem  1. EXIGE o SDK no disco, como o abicheck, e diz isso em portugues quando faltar. Tem
rem     o mesmo prazo de validade: so roda enquanto o SDK estiver aqui.
rem  2. FORA do `all`. E ferramenta de verificacao local e nao entra no BcdAsio.dll.
rem  3. NIVEL DE AVISO PADRAO, e nao /W4 /WX como o abicheck. A diferenca e que este alvo
rem     compila um FONTE do SDK (register.cpp), e nao apenas cabecalhos: os avisos dele
rem     nao temos como consertar, e /WX transformaria o alvo em BUILD_FAIL permanente. E a
rem     mesma razao pela qual os fontes do SDK sempre ficaram fora do alvo `strict`. O
rem     nosso asioreg.cpp e conferido em /W4 /WX la, onde ele esta.
:regcheck
if not exist ..\ASIOSDK\common\register.cpp goto :regcheck_nosdk
cl %CFLAGS% %SDKINC% regcheck.cpp asioreg.cpp ..\ASIOSDK\common\register.cpp /Fe:regcheck.exe /link %CORELIBS%
if errorlevel 1           ( echo ===== BUILD_FAIL: regcheck ===== & exit /b 1 )
if not exist regcheck.exe ( echo ===== BUILD_FAIL: regcheck ===== & exit /b 1 )
echo ===== BUILD_OK: regcheck.exe =====
goto :eof

:regcheck_nosdk
echo ===== BUILD_FAIL: regcheck - o ASIO SDK da Steinberg nao esta no disco =====
echo.
echo Este alvo e FERRAMENTA DE VERIFICACAO LOCAL e nao faz parte da build do produto.
echo Ele compara o NOSSO registro do driver ^(native/bcdasio/asioreg.cpp^) com o
echo register.cpp do SDK, chave por chave e byte por byte, e para isso precisa dos DOIS
echo lados. Ele NAO escreve no registro de verdade da maquina.
echo.
echo Ponha o SDK em native/ASIOSDK/ ^(baixe da Steinberg; ver o README^) e rode de novo.
echo Se voce so quer compilar o driver, use os alvos dll, tests, probe ou testaudio.
exit /b 1

rem O alvo `dll` e o PRODUTO, e a lista abaixo e inteira deste projeto: nenhum /I e nenhum
rem fonte de ..\ASIOSDK\common. Saiu `wintimer.cpp` e sairam os quatro fontes do SDK
rem (dllentry.cpp, register.cpp, combase.cpp, debugmessage.cpp); entraram `asioreg.cpp`,
rem `comserver.cpp` e `nanoclock.cpp`.
:dll
cl /LD %CFLAGS% ^
   bcdasio.cpp audioengine.cpp midibridge.cpp usbdev.cpp ringbuf.cpp format.cpp log.cpp handoff.cpp ^
   asioreg.cpp comserver.cpp nanoclock.cpp dllmain.cpp ^
   /link /DEF:BcdAsio.def /OUT:BcdAsio.dll %CORELIBS%
if errorlevel 1          ( echo ===== BUILD_FAIL: dll ===== & exit /b 1 )
if not exist BcdAsio.dll ( echo ===== BUILD_FAIL: dll ===== & exit /b 1 )
echo ===== BUILD_OK: BcdAsio.dll =====
goto :eof
