# Rezultate finale pentru raport

Acest director contine setul recomandat pentru predare. Masuratorile sunt rulate fara oversubscription, pe procesele disponibile local (`P=1,2,4`).

## Fisiere CSV

- `strong_scaling_compute_heavy_5000_50000_ants5000.csv`: strong scaling principal, cu suficient calcul per proces pentru a evidentia paralelizarea.
- `weak_scaling_100000.csv`: weak scaling.
- `migration_overhead_1000_100000.csv`: impactul numarului de furnici asupra timpului total, calculului si comunicarii.
- `gather_frequency_1000_10000.csv`: impactul frecventei colectarii globale.

## Grafice

Graficele pentru raport sunt in `charts/`:

- `compute_heavy_speedup.png`
- `compute_heavy_efficiency.png`
- `compute_heavy_compute_vs_comm.png`
- `weak_scaling.png`
- `migration_overhead.png`
- `gather_frequency.png`

## Validare

Testul mare de corectitudine a fost rulat cu:

```text
N=1000, T=100000, ants=100
secvential vs MPI cu 4 procese
```

Rezultatul a fost identic binar (`cmp` fara diferente). Fisierele PPM generate pentru validare nu sunt incluse in Git deoarece sunt artefacte mari de rulare.
