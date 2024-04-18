# Tabellinator

Dato un file GPX, genera un file PDF con LaTeX che include tabella di marcia, profilo altimetrico e cartina.

## Requisiti

- Per generare il documento LaTeX _senza_ cartina:
  - _niente_
- Per generare il documento LaTeX _con_ cartina:
  - [`ImageMagick`](https://imagemagick.org/script/download.php) (versione `7.0` o superiore) installato e nella variabile `PATH`
  - `cURL` installato e nella variabile `PATH`
- Per generare il documento PDF _senza_ cartina:
  - `XeLaTeX` installato e nella variabile `PATH`
- Per generare il documento PDF _con_ cartina:
  - `XeLaTeX` installato e nella variabile `PATH` 
  - [`ImageMagick`](https://imagemagick.org/script/download.php) (versione `7.0` o superiore) installato e nella variabile `PATH`
  - `cURL` installato e nella variabile `PATH`

> [!WARNING]
> I file (TIFF) delle mappe ufficiali svizzere hanno grandi dimensioni (~380 MB caduno). Assicuratevi di avere una buona connessione e sufficiente spazio sul disco.

## Utilizzo

### Compilazione

```sh
$ cc nobuild.c -o nobuild
$ ./nobuild
```

### Utilizzo

```sh
$ ./tabellinator <path/al/file.gpx> [opzioni]
```

Il programma chiederà poi dati dall'utente quali:
- il fattore di velocità (chilometri sforzo all'ora);
- l'orario di partenza;
- la durata delle pause nei varie tappe intermedie.

#### Opzioni

- `--pdf`: Il programma invoca automaticamente XeLaTeX per generare il file PDF. XeLaTeX deve essere installato perché ciò funzioni.
-  `--map`: Il programma scarica le mappe ufficiali svizzere ([swisstopo](https://www.swisstopo.admin.ch/it), scala 1:25'000), le ritaglia secondo necessità e le include nel documento LaTeX. cURL e ImageMagick devono essere installati perché ciò funzioni.
- `-h`,`--help`: Stampa un messaggio di aiuto, poi termina.