# Configurazione ed esecuzione manuale su CAPRI

Guida preparata il 10 settembre 2026. Tutti i comandi remoti sono da eseguire
manualmente con il tuo account. Non occorre condividere password o chiavi.
I nomi dei moduli software e le risorse finali vanno verificati sulla piattaforma:
gli esempi pubblici possono riferirsi a versioni precedenti.

## 1. Impostare l'accesso dal Mac

Usa il nome del login node e l'account ricevuti dal corso. Se l'accesso richiede
VPN o un passaggio intermedio, segui le istruzioni associate al tuo account.
Puoi aggiungere un alias al tuo `~/.ssh/config`, sostituendo i segnaposto:

```sshconfig
Host capri
    HostName capri.dei.unipd.it
    User IL_TUO_ACCOUNT
    ServerAliveInterval 60
```

Se usi già una chiave autorizzata, puoi indicarla con `IdentityFile`; non
copiare la chiave privata sul cluster. Prova l'accesso:

```sh
ssh capri
```

Al primo accesso cambia la password con `passwd`, come indicato a lezione.
Connettiti esclusivamente al login node. Esegui i calcoli attraverso Slurm.
Queste regole e il riconoscimento della piattaforma nei prodotti scientifici
sono previsti dal [regolamento CAPRI](https://capri.dei.unipd.it/regulation/).

## 2. Verificare risorse e ambiente, senza avviare calcoli

Sul login node:

```sh
sinfo
scontrol show partition allgroups
module avail
command -v spack
```

Se è disponibile Spack, `spack find` elenca il software installato. Individua
un compilatore C11 e una MPI compatibile. L'esempio MPI della
[guida CAPRI](https://capriuserguide.readthedocs.io/en/latest/SLURMExamples.html#mpi-job)
usa un ambiente Intel storico: non copiarne automaticamente nome e versione.

Il laboratorio usa l'Open MPI di sistema, già nel PATH senza `module load`:

```sh
command -v gcc mpicc mpirun
gcc --version
mpicc --version
```

Per conoscere il compilatore richiamato dal wrapper, usa `mpicc -show` oppure,
con Open MPI, `mpicc --showme`. Scegli `CC` coerente con il compilatore usato da
`MPICC`, così le opzioni di ottimizzazione sono comparabili.

I programmi MPI si lanciano con `mpirun` dentro l'allocazione Slurm, come
mostrato a lezione. Non dedurre la compatibilità dal solo fatto che `mpicc`
compila: il job di verifica del punto 5 controlla anche l'avvio effettivo di
più rank.

## 3. Copiare il progetto

Dal Mac, nella cartella `project/` del repository:

```sh
ssh capri 'mkdir -p ~/unipd-hpc'
rsync -av --exclude .venv --exclude dist \
  --exclude fft2d_seq --exclude fft2d_mpi \
  --exclude test_seq --exclude test_mpi \
  --exclude results --exclude 'report/generated' \
  --exclude 'report/*.pdf' --exclude compile_flags.txt \
  --exclude src/scripts/capri-env.sh \
  ./ capri:~/unipd-hpc/
```

Attenzione:
`.git` vive alla radice del repository e non viene copiato, quindi i
metadati raccolti sul cluster registrano la revisione come `unknown`;
annota a mano la revisione locale da cui hai sincronizzato, preferibilmente
committata prima della campagna finale.
Non trasferire gli eseguibili macOS: verranno ricompilati su CAPRI. Non avviare
due job che ricompilano contemporaneamente nella stessa cartella.

## 4. Salvare l'ambiente del job

Sul login node:

```sh
cd ~/unipd-hpc
cp src/scripts/capri-env.example.sh src/scripts/capri-env.sh
nano src/scripts/capri-env.sh
```

Inserisci i comandi `module load` o `spack load` verificati al punto 2, includendo
l'eventuale inizializzazione necessaria nella shell batch. Configura:

```sh
export CC=gcc
export MPICC=mpicc
export CFLAGS='-O3 -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion'
```

Questi sono valori iniziali: adegua compiler/wrapper e gli eventuali flag MPI
all'ambiente scelto. Il file è escluso da Git e non deve contenere credenziali.
Gli script Slurm lanciano i programmi MPI con `mpirun` senza opzioni di
binding: l'allocazione Slurm confina già il job sui core assegnati, e un
binding esplicito per core entra in conflitto con quel confinamento.

## 5. Compilare e verificare attraverso Slurm

Il file `src/scripts/capri-check.slurm` richiede inizialmente 4 task, 1 CPU per task,
1 GiB per nodo e 5 minuti, ed esegue compilazione e test sui nodi assegnati.
Verifica che queste richieste siano compatibili con il tuo account, poi invia:

```sh
sbatch src/scripts/capri-check.slurm
squeue -u "$USER"
```

Annota il JOBID restituito. Dopo la conclusione:

```sh
cat slurm-JOBID.out
cat slurm-JOBID.err
seff JOBID
```

Devono passare le suite C con 1, 2 e 4 rank e le verifiche degli input non
validi. Queste ultime provocano volutamente exit code non nulli in alcuni step;
il job complessivo deve comunque terminare con successo. Gli ultimi due comandi
stampano righe CSV sequenziali e MPI. Se il launcher segnala errori,
rivedi l'ambiente in `capri-env.sh`, poi ripeti il job di verifica.

`allgroups` e le richieste esplicite di task/partizione/tempo/memoria con
`seff` a posteriori seguono la [guida Slurm CAPRI](https://capriuserguide.readthedocs.io/en/latest/UsingSLURM.html).

## 6. Eseguire il primo pilot

Il pilot iniziale usa `N=4096`, seed 42, 3 invocazioni per punto e
`P=1,2,4,8`. Le matrici sono quindi 4096x4096, 2048x8192 e 8192x2048. È un
punto di partenza operativo, non la scelta della campagna finale: sul Mac le
matrici piccole (fino a ~1024) misurano quasi solo comunicazione, e su CAPRI
il singolo core sarà probabilmente più lento, non più veloce.

```sh
sbatch src/scripts/capri-benchmark.slurm
```

Lo script ricompila sul nodo assegnato e crea
`results/capri-pilot-JOBID/`. Ogni invocazione misura una sola trasformata.
Sono salvati `raw.csv` e `metadata.txt` (configurazione, launcher, revisione
Git, versioni). In caso di errore i dati parziali restano disponibili, ma il
log Slurm registra il fallimento: non usarli come campagna finale.

Per un pilot successivo, scegli una nuova dimensione e una lista di processi
giustificata dai primi risultati. Per esempio, **solo se autorizzati e utili**,
16 processi e `N=8192`:

```sh
export BASE_N=8192 RUNS=3 PROCESSES='1 2 4 8 16'
sbatch --ntasks=16 --time=00:20:00 --mem=4G src/scripts/capri-benchmark.slurm
```

Le opzioni mostrate sono esempi di pilot, da adeguare con le misure. Gli script
ereditano le variabili esportate. Per tornare ai valori predefiniti:

```sh
unset BASE_N RUNS PROCESSES CAMPAIGN
```

## 7. Scegliere N, processi, memoria e tempo

Recupera un pilot sul Mac seguendo il punto 9, poi esegui `make analyze` sul
relativo percorso. Controlla minimo, mediana e massimo dei tempi per ogni punto.
Se i tempi diventano troppo brevi o la dispersione è elevata, prova un N più
grande prima di fissare la campagna. Il massimo P è sempre una potenza di due
non maggiore di N/2, perché deve essere valido per tutte le tre forme.

Con `M=16*N*N` byte di matrice, le allocazioni principali sono:

- sequenziale: `2*M`;
- MPI, ciascun rank: `4*M/P` (ogni rank genera direttamente le proprie righe,
  nessun rank alloca l'intera matrice).

Se tutti i rank sono sullo stesso nodo, il picco complessivo degli array MPI è
`4*M`. Per N=4096 sono circa 1 GiB, per N=8192 circa 4 GiB, per N=16384 circa
16 GiB. Aggiungi MPI, runtime e un margine prudente; controlla le misure con
`seff`. Su più nodi, considera quanti rank si trovano su ciascun nodo.
`--mem` è una richiesta **per nodo**, come
specificato nella [documentazione sbatch](https://slurm.schedmd.com/sbatch.html#OPT_mem).

Il tempo batch include compilazione, generazione e avvio di ogni
processo: non stimarlo usando soltanto il tempo kernel del CSV. Con K conteggi
MPI, il numero di invocazioni è `3 * RUNS * (1 + K)`. Usa il tempo totale dei
pilot e un margine per la richiesta finale. Non servono 128 o 256 processi a
priori; esplora 8, 16 e oltre soltanto se le risorse e le prime misure lo
giustificano. Conserva saturazioni e rallentamenti.

Prima della campagna finale annota N, lista P, memoria, tempo e motivazione
della scelta in un file nella cartella dei risultati. Mantieni fisso il
launcher e le sue opzioni tra i punti. I metadati registrano launcher,
revisione Git e job Slurm; non
è richiesta una campagna separata sulla collocazione dei rank.

## 8. Campagna finale

Quando hai scelto i valori, esportali e invia il job. Nel seguente esempio
`8192`, `1 2 4 8 16`, `16`, `00:30:00` e `4G` vanno sostituiti con i valori
determinati dal tuo pilot:

```sh
export BASE_N=8192 RUNS=20 PROCESSES='1 2 4 8 16' SEED=42 CAMPAIGN=final
sbatch --job-name=fft-final --ntasks=16 --time=00:30:00 --mem=4G \
  src/scripts/capri-benchmark.slurm
```

La directory sarà `results/capri-final-JOBID/`. Il programma esegue venti
baseline sequenziali per ciascuna forma e venti invocazioni MPI per ogni P,
comprese quelle con P=1. Non cambiare la lista dopo aver visto soltanto i punti
favorevoli. Non mescolare risultati provenienti da N, seed, revisioni o flag
diversi nella stessa analisi.

## 9. Recuperare e analizzare sul Mac

Dopo il job, sul login node:

```sh
seff JOBID
```

Dal Mac, nella cartella `project/` del repository:

```sh
rsync -av capri:~/unipd-hpc/results/capri-final-JOBID/ results/capri-final-JOBID/
scp capri:~/unipd-hpc/slurm-JOBID.out results/capri-final-JOBID/
scp capri:~/unipd-hpc/slurm-JOBID.err results/capri-final-JOBID/
ssh capri 'seff JOBID' > results/capri-final-JOBID/seff.txt
uv sync --locked
make analyze RESULTS=results/capri-final-JOBID
```

Sostituisci JOBID anche dentro gli apici. Per un pilot usa invece il percorso
`capri-pilot-JOBID`. Non è necessario installare Python o LaTeX sul cluster.

L'analisi produce `summary.csv` e tre grafici `speedup-square`, `speedup-wide`,
`speedup-tall`, in PDF e PNG. La velocizzazione è il rapporto delle mediane
sequenziale/MPI; le tabelle includono min/max e compute fraction. Quest'ultima
misura la frazione dedicata alle FFT locali: il complemento include packing,
unpacking, collettive e attese, non soltanto rete.

## 10. Report e archivio

Sul Mac:

```sh
make report RESULTS=results/capri-final-JOBID CAPRI_FINAL=1
```

Apri `report/report.pdf`. Le tabelle e i grafici sono generati dai CSV. Completa
la discussione con le osservazioni del pilot, l'ambiente scelto, la stabilità e
i limiti delle tre curve, senza attribuire al rapporto d'aspetto differenze non
isolate dall'esperimento. I requisiti del corso (slide 11 delle slide introduttive del
laboratorio) sono: 3-8 pagine a colonna singola, font di almeno
10 pt, inglese, PDF; il sorgente usa IEEEtran in modalità a colonna
singola. Il sorgente include la formula di acknowledgement CAPRI.

```sh
make dist RESULTS=results/capri-final-JOBID
```

L'archivio `dist/parallel-2d-fft.zip` contiene esattamente la consegna:
`fft2d_seq.c`, `fft2d_mpi.c` e `report.pdf`, rigenerato dalla campagna indicata
ed etichettato come finale. La creazione viene rifiutata se la cartella non è
una campagna `results/capri-final-JOBID`. Segui eventuali istruzioni di
consegna Moodle più recenti.
