"""Confere, MECANICAMENTE, se as listagens de codigo do plano batem com os arquivos
que estao no disco.

Sem acentuacao de proposito, como o resto do Python deste projeto: a saida vai para um
console cp1252 e acento vira caractere quebrado exatamente na linha que alguem precisa
ler quando algo falha.

POR QUE ISTO EXISTE, E POR QUE ESTA VERSIONADO
As ultimas dez rodadas de trabalho deste projeto afirmaram "plano conferido por diff
mecanico". O script que fazia essa conferencia NUNCA foi commitado: cada rodada o
reescrevia de memoria, e a ultima chegou a um censo diferente da anterior (66/33 contra
66/32) sem conseguir recuperar qual era o criterio. Evidencia que ninguem pode
reproduzir nao e evidencia - e o mesmo defeito que os arnes deste projeto ja tiveram e
que foi corrigido de vez quando eles passaram a ser arquivos versionados. Este arquivo e
a correcao equivalente para a conferencia do plano.

O 66/33, RECONSTRUIDO - E A CONTRADICAO DELE
O criterio antigo nao ficou escrito, mas deu para reconstruir por aritmetica. O plano em
41fc7ce tem exatamente 99 blocos cuja linguagem e codigo (cpp, python, bat, gitignore) -
os demais sao comandos de shell e saida esperada. Classificando cada um desses 99 por
"o texto do bloco aparece LITERALMENTE dentro de algum arquivo de codigo do repositorio",
sai 67 contra 32. Ou seja: o denominador do criterio antigo era esse 99, e as duas
rodadas contaram a MESMA coisa com um erro de uma unidade cada.

E aqui esta a contradicao que ninguem viu na epoca: 66+33 da 99, mas 66+32 da **98**.
Com o denominador fixo em 99, o segundo par e aritmeticamente impossivel - nao era uma
divergencia de conteudo, era uma conta errada. Nenhuma das duas rodadas podia perceber,
porque nenhuma das duas publicou o denominador.

Este script imprime DOIS censos, e por isso: o censo A (o criterio novo, com manifesto,
que decide o codigo de saida) e o censo B (a reconstrucao acima, so como indicador de
deriva, sem influenciar o codigo de saida - um fragmento pode legitimamente nao aparecer
literal, porque varios sao trechos reordenados com reticencias). O censo B existe para
que o numero de hoje seja comparavel com o das rodadas antigas, agora que o denominador
esta publicado ao lado dele.

O CRITERIO DO CENSO A, POR EXTENSO

1. Um "bloco" e um trecho cercado por tres acentos graves no comeco da linha.

2. Um bloco e uma LISTAGEM DE ARQUIVO INTEIRO quando a linha nao vazia imediatamente
   anterior a ele comeca com "Criar" E contem um caminho entre acentos graves. Essa e a
   convencao que o proprio plano usa para dizer "este bloco e o conteudo COMPLETO deste
   arquivo". Todo o resto - comandos de shell, fragmentos introduzidos por
   "Em `x`, acrescentar...", tabelas de saida esperada - e contado e NAO e comparado,
   porque nao pretende ser o arquivo inteiro.

3. A comparacao e byte a byte depois de duas normalizacoes, e so estas duas: CRLF vira
   LF, e garante-se exatamente uma quebra de linha no fim dos dois lados. Nenhuma outra
   diferenca e tolerada - nem espaco no fim da linha, nem indentacao.

4. Cada listagem encontrada tem de estar CLASSIFICADA no MANIFESTO abaixo, pela chave
   (caminho, n-esima ocorrencia daquele caminho no plano). Quatro classes:

     VIVA         - a listagem E o conteudo atual do arquivo. Divergir e FALHA.
     INCREMENTAL  - o plano cria o arquivo aqui e o estende em passos posteriores
                    ("Em `x`, acrescentar..."). A listagem e a PRIMEIRA versao e nunca
                    pretendeu ser a final. Nao comparada.
     HISTORICA    - o arquivo evoluiu depois, e o plano decidiu NAO reescrever esta
                    listagem; a evolucao esta documentada em outra secao do plano. Nao
                    comparada. Esta classe exige que o plano diga isso em algum lugar -
                    nao e lugar para esconder deriva.
     TEMPORARIA   - arquivo que o plano cria para uma verificacao e apaga no mesmo
                    passo. Aqui a conferencia se INVERTE: o arquivo tem de NAO existir.

   Uma listagem sem classificacao e FALHA. E de proposito: sem essa regra, acrescentar
   uma listagem nova ao plano passaria sem ninguem decidir se ela deve bater com o
   disco - que e exatamente como uma conferencia mecanica vira teatro.

5. Codigo de saida: 0 se toda VIVA bate, toda TEMPORARIA esta ausente e toda listagem
   esta classificada. 1 em qualquer outro caso. So o censo A decide isto.

O CRITERIO DO CENSO B, POR EXTENSO

   Todo bloco cuja linguagem e codigo (cpp, python, bat, gitignore) - portanto NAO os
   blocos de comando de shell nem os de saida esperada - e classificado por: o texto do
   bloco, sem as quebras de linha do fim, aparece como trecho CONTIGUO dentro de algum
   arquivo de codigo do repositorio? Os arquivos varridos sao os de extensao .h, .cpp,
   .py, .bat e .def mais o .gitignore, com native/ASIOSDK e "Drivers originais"
   EXCLUIDOS de proposito: nao sao nossos, e um bloco do plano que casasse com um
   arquivo de la seria coincidencia enganosa.

   O censo B nao decide codigo de saida nenhum. Ele e indicador de deriva, e o numero
   que importa nele e o DENOMINADOR: se o total de blocos de codigo mudar sem que
   alguem tenha acrescentado listagem, alguma coisa se perdeu no caminho.

USO
    python tools/plan_diff.py            # confere e imprime os dois censos
    python tools/plan_diff.py -v         # lista tambem as listagens que passaram
"""

import io
import os
import re
import sys

FENCE = "`" * 3

PLAN_DEFAULT = "docs/superpowers/plans/2026-07-27-bcd3000-audio-asio.md"

VIVA        = "VIVA"
INCREMENTAL = "INCREMENTAL"
HISTORICA   = "HISTORICA"
TEMPORARIA  = "TEMPORARIA"

# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
# MANIFESTO
#
# Chave: (caminho como o plano escreve, numero da ocorrencia daquele caminho, contando
# de 1 na ordem do documento). Valor: (classe, motivo).
#
# O motivo nao e enfeite: e o que a proxima rodada vai ler em vez de adivinhar.
# - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
MANIFEST = {
    ("native/bcdasio/tests.cpp", 1): (
        INCREMENTAL,
        "Tarefa 1 cria com o teste de log; as Tarefas 3, 4, 5, 6, 7, 8, 9 e 10 "
        "acrescentam funcoes de teste em passos 'Em tests.cpp, acrescentar'"),

    ("native/bcdasio/log.h", 1):   (VIVA, "Tarefa 1; nunca mais alterado"),
    ("native/bcdasio/log.cpp", 1): (VIVA, "Tarefa 1; nunca mais alterado"),

    ("native/bcdasio/hdrcheck.cpp", 1): (
        TEMPORARIA,
        "Tarefa 2 cria so para conferir que os cabecalhos de WinUSB compilam, e apaga "
        "no mesmo passo"),

    ("native/bcdasio/usbdev.h", 1): (
        INCREMENTAL,
        "Tarefa 2 cria; a Tarefa 7 acrescenta devicePath(), a 8 mexe nas constantes e "
        "a 9 troca a assinatura de open()"),
    ("native/bcdasio/usbdev.cpp", 1): (
        HISTORICA,
        "listagem historica declarada pelo proprio plano na 'Rodada de correcao da "
        "revisao final'; a evolucao vive la"),

    ("native/bcdasio/probe.cpp", 1): (
        VIVA,
        "Tarefa 2; a Tarefa 13 trocou as constantes de endpoint pelos campos do perfil "
        "do aparelho, e a listagem FOI ressincronizada no mesmo passo - por isso ela "
        "segue VIVA, e nao HISTORICA. O 'nunca mais alterado' que estava aqui deixou de "
        "ser verdade em 2026-07-29"),

    ("native/bcdasio/format.h", 1):   (VIVA, "Tarefa 3; nunca mais alterado"),
    ("native/bcdasio/format.cpp", 1): (VIVA, "Tarefa 3; nunca mais alterado"),

    ("native/bcdasio/ringbuf.h", 1):   (VIVA, "Tarefa 4; nunca mais alterado"),
    ("native/bcdasio/ringbuf.cpp", 1): (VIVA, "Tarefa 4; nunca mais alterado"),

    ("native/bcdasio/audioengine.h", 1): (
        VIVA,
        "Tarefa 5; a listagem FOI ressincronizada na rodada de correcao, conforme o "
        "plano registra - por isso VIVA e nao INCREMENTAL, apesar de existir passo "
        "posterior que a altera"),
    ("native/bcdasio/audioengine.cpp", 1): (
        HISTORICA,
        "listagem historica declarada pelo proprio plano na 'Rodada de correcao da "
        "revisao final'; a evolucao vive la"),

    ("native/bcdasio/testaudio.cpp", 1): (
        INCREMENTAL,
        "Tarefa 5 cria; a Tarefa 6 insere a classe de captura e troca o despacho do "
        "main"),

    ("native/bcdasio/bcdasio.h", 1): (
        INCREMENTAL,
        "Tarefa 7 cria; a Tarefa 8 acrescenta o include e o membro da ponte MIDI"),
    ("native/bcdasio/bcdasio.cpp", 1): (
        HISTORICA,
        "listagem historica declarada pelo proprio plano na 'Rodada de correcao da "
        "revisao final'; a evolucao vive la"),

    # ---- Tarefa 11: declaracao propria da interface ASIO ----
    ("native/bcdasio/asioapi.h", 1): (
        VIVA,
        "Tarefa 11; e a fonte da verdade da fronteira binaria e o abicheck depende "
        "dela byte a byte"),
    ("native/bcdasio/abicheck.cpp", 1): (
        VIVA,
        "Tarefa 11; a prova em si - uma listagem defasada aqui seria uma prova que "
        "ninguem consegue reproduzir"),
    ("tools/plan_diff.py", 1): (
        VIVA,
        "Tarefa 11; este proprio script. Se a listagem dele divergir do disco, o censo "
        "que ele imprime nao e reproduzivel - que e o defeito que ele existe para "
        "consertar"),

    # ---- Tarefa 12: cortar o SDK da Steinberg da compilacao do produto ----
    #
    # As nove listagens desta tarefa sao TODAS VIVA, e nao e por rigor decorativo: sao
    # arquivos NOVOS, criados de uma vez, sem passo posterior que os altere. A convencao
    # da Tarefa 11 e a mesma - listagem cheia para arquivo novo, fragmento "Em `x`,
    # ..." para arquivo modificado -, e e por isso que bcdasio.{h,cpp}, build.bat e
    # tests.cpp NAO ganharam listagem nova aqui: eles foram modificados, e as listagens
    # historicas deles seguem classificadas como estavam.
    ("native/bcdasio/LICENSE-asiosample.txt", 1): (
        VIVA,
        "Tarefa 12, Step 1; e a obrigacao da licenca BSD do exemplo asiosample, de que "
        "bcdasio.{h,cpp} derivam. Uma listagem defasada aqui seria um aviso de licenca "
        "que nao corresponde ao que esta no repositorio"),

    ("native/bcdasio/nanoclock.h", 1):   (VIVA, "Tarefa 12, Step 2"),
    ("native/bcdasio/nanoclock.cpp", 1): (VIVA, "Tarefa 12, Step 2"),

    ("native/bcdasio/asioreg.h", 1): (
        VIVA,
        "Tarefa 12, Step 3; o topo deste arquivo carrega a ESPECIFICACAO do registro "
        "(as cinco entradas medidas na maquina do dono)"),
    ("native/bcdasio/asioreg.cpp", 1): (VIVA, "Tarefa 12, Step 3"),

    ("native/bcdasio/comserver.h", 1):   (VIVA, "Tarefa 12, Step 4"),
    ("native/bcdasio/comserver.cpp", 1): (VIVA, "Tarefa 12, Step 4"),
    ("native/bcdasio/dllmain.cpp", 1): (
        VIVA,
        "Tarefa 12, Step 4; o arquivo ja existia e foi substituido por inteiro. A "
        "listagem usa a convencao 'Criar' de proposito: e ela que faz este script "
        "comparar o bloco com o disco. Com 'Reescrever' o bloco cairia em 'outros' e "
        "ninguem conferiria nada - que e como uma conferencia mecanica vira teatro"),

    ("native/bcdasio/regcheck.cpp", 1): (
        VIVA,
        "Tarefa 12, Step 5; a prova do registro. Mesmo criterio do abicheck.cpp: uma "
        "listagem defasada aqui seria uma prova que ninguem consegue reproduzir"),

    # ---- Tarefa 13: perfil de aparelho, e a BCD2000 como opcao experimental ----
    #
    # SO UMA listagem nova, e a escolha tem motivo. A Tarefa 13 modificou nove arquivos e
    # nao criou nenhum, e a convencao das Tarefas 11 e 12 e clara: listagem cheia para
    # arquivo novo, fragmento "Em `x`, ..." para arquivo modificado. O `usbdev.h` e a
    # excecao porque ele foi REESCRITO e porque e a ESPECIFICACAO do perfil - a estrutura
    # que os outros oito arquivos consomem. Mesmo criterio que fez o `dllmain.cpp` da
    # Tarefa 12 ganhar listagem cheia com a palavra "Criar".
    #
    # O `usbdev.cpp` NAO ganhou listagem nova de proposito, e nao e por deriva: a
    # ocorrencia 1 dele segue HISTORICA (declarado pelo proprio plano na "Rodada de
    # correcao da revisao final"), e o que interessa nele - os VALORES da tabela de
    # perfis - esta travado pelo test_perfis_de_aparelho, comparado com os literais
    # historicos. Isso e mais forte que um diff de texto do plano: um diff pega o arquivo
    # mudar, o teste pega o VALOR mudar.
    #
    # OS DOIS NUMEROS DESSE TESTE, e por que sao dois. Esta nota dizia "40 assercoes", que
    # nao corresponde a nenhuma das duas contagens. Recontado de forma independente:
    #   91 statements CHECK( escritos na funcao;
    #  153 verificacoes EXECUTADAS, porque nove blocos rodam dentro de lacos (por perfil,
    #      por canal, e um laco de pares a<b que sozinho da 24).
    # Publicar os dois e a regra deste arquivo, e nao zelo: um censo sem denominador
    # sobreviveu duas rodadas neste projeto, e foi o que motivou versionar este script.
    ("native/bcdasio/usbdev.h", 2): (
        VIVA,
        "Tarefa 13, Step 1; e a especificacao do perfil de aparelho e o contrato que os "
        "outros oito arquivos modificados consomem. A ocorrencia 1 continua INCREMENTAL "
        "(Tarefa 2 criou, as 7, 8 e 9 alteraram); esta e a reescrita da Tarefa 13"),
}

PATH_IN_BACKTICKS = re.compile(
    r'`([A-Za-z0-9_][A-Za-z0-9_./-]*\.(?:h|cpp|py|bat|def|md|txt|gitignore))`')

# Linguagens que o censo B considera codigo. `bash` e os blocos sem linguagem ficam de
# fora: sao comando de shell e saida esperada, e nunca pretenderam estar em arquivo
# nenhum.
CODE_LANGS = ("cpp", "python", "bat", "gitignore")

# Onde o censo B procura. native/ASIOSDK e "Drivers originais" ficam de fora de
# proposito: nao sao nossos, e um bloco do plano que casasse com um arquivo de la seria
# coincidencia enganosa.
CODE_EXTS = (".h", ".cpp", ".py", ".bat", ".def")
SKIP_DIRS = (".git", "__pycache__", "native/ASIOSDK", "Drivers originais",
             "poc/build", "poc/dist", "native/bcdasio/strict", "dist")


def find_blocks(lines):
    """Devolve [(inicio, fim, linguagem)] com os indices das linhas das cercas."""
    blocks = []
    i = 0
    while i < len(lines):
        if lines[i].startswith(FENCE):
            lang = lines[i][len(FENCE):].strip()
            j = i + 1
            while j < len(lines) and not lines[j].startswith(FENCE):
                j += 1
            blocks.append((i, j, lang))
            i = j + 1
        else:
            i += 1
    return blocks


def previous_non_empty(lines, idx):
    k = idx - 1
    while k >= 0 and lines[k].strip() == "":
        k -= 1
    return lines[k] if k >= 0 else ""


def normalize(text):
    """As DUAS unicas normalizacoes que o criterio permite."""
    text = text.replace("\r\n", "\n")
    while text.endswith("\n\n"):
        text = text[:-1]
    if not text.endswith("\n"):
        text += "\n"
    return text


def first_difference(a, b):
    """Numero da primeira linha (contando de 1) em que os dois textos divergem."""
    al = a.split("\n")
    bl = b.split("\n")
    for n in range(max(len(al), len(bl))):
        x = al[n] if n < len(al) else "<fim do arquivo>"
        y = bl[n] if n < len(bl) else "<fim do arquivo>"
        if x != y:
            return n + 1, x, y
    return 0, "", ""


def repo_code_files(root):
    """Os arquivos de codigo NOSSOS, para o censo B."""
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        rel = os.path.relpath(dirpath, root).replace("\\", "/")
        if rel == ".":
            rel = ""
        if any(rel == d or rel.startswith(d + "/") for d in SKIP_DIRS):
            dirnames[:] = []
            continue
        for name in filenames:
            if name.endswith(CODE_EXTS) or name == ".gitignore":
                out.append(os.path.join(dirpath, name))
    return out


def census_b(root, blocks, lines):
    """Reconstrucao do censo antigo. Devolve (total, literais, nao_literais)."""
    haystack = []
    for path in repo_code_files(root):
        try:
            with io.open(path, encoding="utf-8", newline="") as f:
                haystack.append(f.read().replace("\r\n", "\n"))
        except (IOError, OSError, UnicodeDecodeError):
            pass

    total = literal = 0
    for (start, end, lang) in blocks:
        if lang not in CODE_LANGS:
            continue
        total += 1
        body = "\n".join(lines[start + 1:end]).rstrip("\n")
        if any(body in h for h in haystack):
            literal += 1
    return total, literal, total - literal


def main(argv):
    verbose = "-v" in argv or "--verbose" in argv
    args = [a for a in argv[1:] if not a.startswith("-")]

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    plan = args[0] if args else os.path.join(root, PLAN_DEFAULT)
    if not os.path.exists(plan):
        print("plan_diff: plano nao encontrado: %s" % plan)
        return 1

    lines = io.open(plan, encoding="utf-8", newline="").read().replace(
        "\r\n", "\n").split("\n")
    blocks = find_blocks(lines)

    listings = []
    others = 0
    for (start, end, lang) in blocks:
        intro = previous_non_empty(lines, start)
        match = PATH_IN_BACKTICKS.search(intro)
        if match and intro.lstrip().startswith("Criar"):
            listings.append((match.group(1), start, end, lang))
        else:
            others += 1

    counts = {VIVA: 0, INCREMENTAL: 0, HISTORICA: 0, TEMPORARIA: 0}
    equal = 0
    problems = 0
    seen = {}

    print("== plan_diff: %s ==" % os.path.relpath(plan, root).replace("\\", "/"))
    print("   criterio no topo de tools/plan_diff.py\n")

    for (path, start, end, lang) in listings:
        seen[path] = seen.get(path, 0) + 1
        key = (path, seen[path])
        entry = MANIFEST.get(key)
        where = "linha %5d" % (start + 1)

        if entry is None:
            problems += 1
            print("  NAO CLASSIFICADA  %-34s oc.%d  %s" % (path, seen[path], where))
            print("      Acrescente ('%s', %d) ao MANIFESTO de tools/plan_diff.py, "
                  "com a classe e o motivo." % (path, seen[path]))
            continue

        klass, why = entry
        counts[klass] += 1
        disk_path = os.path.join(root, path)

        if klass == TEMPORARIA:
            if os.path.exists(disk_path):
                problems += 1
                print("  FALHA             %-34s oc.%d  %s  TEMPORARIA mas o arquivo "
                      "EXISTE no disco" % (path, seen[path], where))
            elif verbose:
                print("  ok  TEMPORARIA    %-34s oc.%d  %s  (%s)"
                      % (path, seen[path], where, why))
            continue

        if klass != VIVA:
            if verbose:
                print("  -   %-13s %-34s oc.%d  %s  (%s)"
                      % (klass, path, seen[path], where, why))
            continue

        if not os.path.exists(disk_path):
            problems += 1
            print("  FALHA             %-34s oc.%d  %s  VIVA mas o arquivo NAO existe "
                  "no disco" % (path, seen[path], where))
            continue

        from_plan = normalize("\n".join(lines[start + 1:end]))
        from_disk = normalize(io.open(disk_path, encoding="utf-8", newline="").read())

        if from_plan == from_disk:
            equal += 1
            if verbose:
                print("  ok  VIVA          %-34s oc.%d  %s  (%s)"
                      % (path, seen[path], where, why))
        else:
            problems += 1
            n, got, want = first_difference(from_plan, from_disk)
            print("  FALHA             %-34s oc.%d  %s  VIVA e DIVERGE do disco "
                  "(primeira diferenca na linha %d da listagem)"
                  % (path, seen[path], where, n))
            print("      plano: %s" % got[:110])
            print("      disco: %s" % want[:110])

    print("\n== censo A: %d blocos, %d listagens de arquivo inteiro, %d outros blocos ==" %
          (len(blocks), len(listings), others))
    print("== classes: %d VIVA (%d conferidas iguais), %d INCREMENTAL, %d HISTORICA, "
          "%d TEMPORARIA ==" %
          (counts[VIVA], equal, counts[INCREMENTAL], counts[HISTORICA],
           counts[TEMPORARIA]))

    b_total, b_lit, b_not = census_b(root, blocks, lines)
    print("== censo B (reconstrucao, nao decide nada): %d blocos de codigo, "
          "%d literais no disco, %d nao literais ==" % (b_total, b_lit, b_not))

    if problems:
        print("\nPLAN_DIFF_FAIL: %d problema(s). O plano NAO esta sincronizado com o "
              "disco." % problems)
        return 1
    print("\nPLAN_DIFF_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
