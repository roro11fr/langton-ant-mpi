# Langton Ant 2D cu MPI

Proiect C++17/CMake pentru simularea automatului celular Furnica lui Langton in 2D. Include versiune secventiala, versiune MPI, suport multi-furnica, rezolvare conflicte, export PPM, cadre pentru animatie si benchmark-uri pentru analiza performantei.

## Ce face proiectul

Simuleaza o grila `N x N` de celule albe/negre pe care se deplaseaza una sau mai multe furnici. La fiecare pas:

1. Furnica citeste culoarea celulei curente.
2. Pe celula alba se roteste la dreapta; pe celula neagra se roteste la stanga.
3. Celula curenta isi inverseaza culoarea.
4. Furnica avanseaza un pas in directia noua.

Pentru mai multe furnici, toate deciziile se calculeaza pe starea curenta a grilei. Daca doua sau mai multe furnici modifica aceeasi celula in acelasi pas, conflictul este rezolvat prin politica aleasa:

- `modulo`: numarul de flip-uri se aplica modulo 2; doua flip-uri se anuleaza, trei flip-uri inseamna un flip efectiv;
- `cancel`: orice conflict cu mai mult de o furnica anuleaza modificarea celulei;
- `priority`: conflictul este redus la un singur flip, asociat conceptual furnicii cu ID minim.

## Cerinte acoperite din PDF

- implementare secventiala completa;
- implementare MPI cu model SPMD;
- partitionare 1D a domeniului pe randuri;
- distribuirea grilei initiale cu `MPI_Scatterv`;
- ghost rows schimbate cu `MPI_Sendrecv`;
- ghost rows consultate explicit la tratarea furnicilor care parasesc randul de frontiera al partitiei;
- migrare dinamica a furnicilor intre procese;
- mesaje de dimensiune variabila pentru furnici, receptionate cu `MPI_Probe` si `MPI_Get_count`;
- comunicare asincrona la migrare cu `MPI_Isend`;
- rezolvare conflicte configurabila: `modulo`, `cancel`, `priority`;
- colectare finala si periodica prin `MPI_Gatherv`;
- export PPM pentru vizualizare;
- cadre PPM periodice pentru animatie;
- timere cu `MPI_Wtime`: calcul, comunicare si I/O;
- scripturi pentru strong scaling, weak scaling, overhead de migrare si frecventa de gather.

## Build

Cerinte:

- Linux sau WSL/cluster HPC;
- CMake 3.16+;
- compilator C++17;
- OpenMPI sau MPICH.

Pe Ubuntu/Debian:

```bash
sudo apt-get install cmake g++ openmpi-bin libopenmpi-dev make
```

Build:

```bash
cmake -S . -B build
cmake --build build
```

## Rulare

Secvential:

```bash
./build/langton_ant --mode seq --size 1000 --steps 100000 --ants 1 --output out_seq.ppm
```

MPI:

```bash
mpirun -np 4 ./build/langton_ant --mode mpi --size 1000 --steps 100000 --ants 100 --output out_mpi.ppm
```

Cu cadre pentru animatie:

```bash
mpirun -np 4 ./build/langton_ant \
  --mode mpi \
  --size 500 \
  --steps 10000 \
  --ants 20 \
  --gather-every 100 \
  --frames-prefix frames/langton
```

Cadrele vor fi scrise ca `frames/langton_000100.ppm`, `frames/langton_000200.ppm` etc. Directorul trebuie creat inainte:

```bash
mkdir -p frames
```

## Optiuni CLI

```text
--mode seq|mpi
--size N
--steps T
--ants K
--seed S
--output file.ppm
--frames-prefix prefix
--conflict-policy modulo|cancel|priority
--gather-every G
--torus true|false
```

`--torus true` este implicit. Pentru `--torus false`, furnicile care ies din grila sunt eliminate.

## Cum functioneaza codul

### Versiunea secventiala

Fisierul `src/simulation.cpp` tine toata grila intr-un vector liniar de `uint8_t`, unde `0` inseamna alb si `1` inseamna negru. Pozitia `(row, col)` este transformata in index prin:

```text
index = row * size + col
```

La fiecare pas se parcurge lista de furnici, se calculeaza noua directie, apoi se memoreaza cate flip-uri se aplica pe fiecare celula. Dupa ce toate furnicile au fost procesate, flip-urile sunt aplicate conform politicii `--conflict-policy`. Aceasta ordine evita rezultate dependente de ordinea furnicilor.

### Versiunea MPI

Fisierul `src/mpi_simulation.cpp` imparte grila pe randuri. Fiecare rank detine doar un interval continuu de randuri:

```text
rank 0: randuri 0 ... a
rank 1: randuri a+1 ... b
rank 2: randuri b+1 ... c
```

Furnicile sunt stocate numai pe rank-ul care detine randul lor curent. Dupa fiecare pas:

1. rank 0 construieste grila initiala si o distribuie cu `MPI_Scatterv`;
2. fiecare rank schimba ghost rows cu vecinii prin `MPI_Sendrecv`;
3. rank-ul calculeaza pasul pentru furnicile locale;
4. pentru furnicile aflate pe randurile de frontiera, ghost rows sunt consultate explicit la detectarea trecerii catre partitia vecina;
5. aplica flip-urile locale conform politicii de conflict alese;
6. trimite furnicile care au iesit din partitia locala;
7. primeste furnici migrate folosind `MPI_Probe`, pentru ca numarul de furnici primite variaza de la pas la pas.

Pentru grila toroidala, primul rank comunica circular cu ultimul rank. Pentru `--torus false`, furnicile care ies din domeniul global sunt eliminate.

### Colectare si output

La final, rank 0 colecteaza grila cu `MPI_Gatherv` si furnicile cu un gather de mesaje serializate. Daca se seteaza `--gather-every`, aceeasi colectare se face periodic. Daca exista si `--frames-prefix`, rank 0 scrie cate un fisier PPM pentru fiecare cadru.

Programul afiseaza o linie de forma:

```text
mode=mpi size=100 steps=2000 ants_initial=50 ants_final=50 processes=4 elapsed_seconds=0.024 compute_seconds=0.010 communication_seconds=0.008 io_seconds=0.002 steps_per_second=81234
```

Valorile `compute_seconds`, `communication_seconds` si `io_seconds` sunt utile pentru capitolul de analiza a performantei.

## Validare

Compara versiunea secventiala cu MPI pe un singur proces:

```bash
./build/langton_ant --mode seq --size 50 --steps 1000 --ants 5 --seed 7 --output seq.ppm
mpirun -np 1 ./build/langton_ant --mode mpi --size 50 --steps 1000 --ants 5 --seed 7 --output mpi1.ppm
cmp seq.ppm mpi1.ppm
```

Compara secventialul cu MPI pe mai multe procese:

```bash
mpirun -np 4 ./build/langton_ant --mode mpi --size 50 --steps 1000 --ants 5 --seed 7 --output mpi4.ppm
cmp seq.ppm mpi4.ppm
```

Testeaza margini fixe:

```bash
./build/langton_ant --mode seq --size 30 --steps 500 --ants 20 --seed 3 --torus false --output fixed_seq.ppm
mpirun -np 3 ./build/langton_ant --mode mpi --size 30 --steps 500 --ants 20 --seed 3 --torus false --gather-every 100 --output fixed_mpi.ppm
cmp fixed_seq.ppm fixed_mpi.ppm
```

Testeaza stres de migrare:

```bash
mpirun -np 4 ./build/langton_ant --mode mpi --size 100 --steps 10000 --ants 200 --seed 11 --gather-every 1000 --output stress.ppm
```

## Benchmark

Benchmark simplu strong scaling:

```bash
chmod +x scripts/run_benchmarks.sh
PROCS="1 2 4 8" SIZE=5000 STEPS=100000 ANTS=100 ./scripts/run_benchmarks.sh
```

Pe WSL sau laptop, daca OpenMPI raporteaza prea putine sloturi:

```bash
OVERSUBSCRIBE=1 PROCS="1 2 4 8" SIZE=1000 STEPS=10000 ANTS=100 ./scripts/run_benchmarks.sh
```

Campania completa de experimente din PDF:

```bash
chmod +x scripts/run_experiments.sh
PROCS="1 2 4 8 16" STEPS=100000 ./scripts/run_experiments.sh
```

Scriptul scrie fisiere CSV in `results/`:

- `strong_scaling.csv`
- `weak_scaling.csv`
- `migration_overhead.csv`
- `gather_frequency.csv`

Pentru grafice:

```bash
python3 scripts/plot_results.py --results-dir results_final --charts-dir results_final/charts
```

Graficele generate:

- `strong_speedup.png`
- `strong_efficiency.png`
- `strong_elapsed.png`
- `compute_communication_io.png`
- `weak_scaling.png`
- `migration_overhead.png`
- `gather_frequency.png`

## Rezultate finale locale

Rezultatele recomandate pentru raport sunt in `results_final/`. Acestea au fost rulate fara oversubscription, folosind doar procesele disponibile local (`P=1,2,4`). Setul include:

- strong scaling pentru `N=500`, `N=1000`, `N=5000`, cu `T=100000`;
- weak scaling cu `T=100000`;
- overhead de migrare pentru `ants=1,10,100,1000`;
- impactul frecventei de colectare pentru `gather_every=1,10,100,1000`;
- comparatie de corectitudine `seq` vs. `MPI 4 procese` pe `N=1000`, `T=100000`, `ants=100`.

Nota: rezultatele anterioare din `results_large/` includ si rulari `P=8/16` cu oversubscription in WSL. Pentru concluzii curate de performanta locala, foloseste `results_final/`. Pentru `P=8/16` corect, reruleaza acele cazuri pe cluster real.

## Fisiere importante

- `src/main.cpp`: CLI, initializare MPI, broadcast configuratie, afisare rezultate.
- `src/simulation.cpp`: implementarea secventiala.
- `src/mpi_simulation.cpp`: partitionare, ghost rows, migrare, gather, timere MPI.
- `src/ppm.cpp`: export imagine PPM.
- `scripts/run_benchmarks.sh`: benchmark strong scaling cu speedup, eficienta si fractie seriala.
- `scripts/run_experiments.sh`: experimentele recomandate in PDF.
- `scripts/plot_results.py`: generare grafice pentru raport.
