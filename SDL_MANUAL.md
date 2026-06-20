# Manuale d'Uso: Subliminal Downloader (SDL)

**Versione:** 0.56.0
**Autore:** Antigravity (Analisi del codice)

Subliminal Downloader (SDL) è una potente utility da riga di comando scritta in C per il download di file. È progettata per gestire download singoli, multipli, e supporta nativamente la risoluzione di link Gofile.io, offrendo al contempo una barra di progresso visuale e capacità di ripristino dei download interrotti.

---

## Indice
1. [Requisiti e Compilazione](#requisiti-e-compilazione)
2. [Utilizzo Base](#utilizzo-base)
3. [Modalità Multi-Download](#modalità-multi-download)
4. [Gestione della Coda (Queue)](#gestione-della-coda)
5. [Opzioni Avanzate](#opzioni-avanzate)
6. [Integrazione Gofile.io](#integrazione-gofileio)
7. [Gestione Conflitti e Ripristino](#gestione-conflitti-e-ripristino)
8. [Esempi Pratici](#esempi-pratici)

---

## 1. Requisiti e Compilazione

Il programma dipende dalla libreria **libcurl**.

### Installazione Dipendenze
- **Debian/Ubuntu:**
  ```bash
  sudo apt-get update
  sudo apt-get install libcurl4-openssl-dev pkg-config
  ```
- **openSUSE:**
  ```bash
  sudo zypper install libcurl-devel
  ```

### Compilazione
Per compilare il programma, eseguire il seguente comando nella directory del sorgente:
```bash
gcc sdl.c -o sdl $(pkg-config --libs --cflags libcurl)
```

---

## 2. Utilizzo Base

Il comando base per scaricare un singolo file è:
```bash
./sdl --url "https://esempio.com/file.zip"
```

### Opzioni di Versione
Visualizza la versione corrente:
```bash
./sdl -v
# oppure
./sdl --version
```

---

## 3. Modalità Multi-Download

SDL permette di gestire più download contemporaneamente o in sequenza.

### Input multiplo da riga di comando
```bash
./sdl --multi "URL1" "URL2" "URL3"
```

### Input da file
Legge una lista di URL da un file di testo (uno per riga):
```bash
./sdl -f lista_url.txt
# oppure
./sdl --file lista_url.txt
```

### Controllo dei Processi Parallelizzati (`--jobs`)
È possibile scegliere come gestire i download multipli:
- **Sequenziale:** `-j code` o `-j sequential`. Scarica un file alla volta.
- **Parallelo (Default per multi):** `-j parallel`. Scarica tutti i file simultaneamente.

---

## 5. Opzioni Avanzate (Note: Section 4 is below for better flow)

| Opzione | Descrizione |
| :--- | :--- |
| `-d`, `--destination <dir>` | Specifica la cartella di destinazione. Se non esiste, verrà chiesta conferma per crearla. |
| `-l`, `--limit-download <MB>` | Limita la velocità di download per singola connessione (es. `1.5` per 1.5 MB/s). |
| `-aw`, `--always-overwrite` | Sovrascrive automaticamente i file esistenti senza chiedere conferma. |
| `--nobar` | Disabilita la barra di progresso visuale (utile per log o terminali limitati). Mostra solo percentuale e velocità. |

---

## 4. Gestione della Coda (Queue)

SDL gestisce una coda persistente salvata nel file `dburl.txt`.

- **Aggiungere un URL alla coda:**
  ```bash
  ./sdl -a "https://esempio.com/file.zip"
  ```
- **Listare il contenuto della coda:**
  Mostra lo stato (Pendente, Completato, Parziale) e la dimensione dei file parziali.
  ```bash
  ./sdl -L
  # oppure
  ./sdl --list
  ```
- **Elaborare la coda:**
  ```bash
  ./sdl -Q
  ```
- **Elaborare ID specifici o range:**
  ```bash
  ./sdl -Q "1,3-5,7"
  ```
  *Nota: I download completati con successo vengono rimossi automaticamente dalla coda.*

---

## 6. Integrazione Gofile.io

SDL include una logica specifica per gestire i link di **Gofile.io** (formato `gofile.io/d/XXXXX`).
1. **Risoluzione Automatica:** Il programma contatta l'API di Gofile.
2. **Guest Token:** Ottiene automaticamente un token guest per poter accedere ai file.
3. **Direct Links:** Converte i link della pagina in link diretti per il download immediato.

---

## 7. Gestione Conflitti e Ripristino

### Ripristino (Resume)
Se un download viene interrotto, SDL crea un file `.part`. Al riavvio dello stesso download, il programma rileva il file parziale e tenta di riprendere dal punto in cui si era fermato (utilizzando l'header HTTP `Range`).

### Conflitti di File
Se il file di destinazione esiste già, SDL chiederà interattivamente:
- **[O]verwrite:** Sovrascrive il file.
- **[C]opy:** Rinonima il nuovo file aggiungendo un numero (es. `file(1).zip`).
- **[S]kip:** Salta il download.

---

## 8. Esempi Pratici

**Download parrallelo con limite di velocità in una cartella specifica:**
```bash
./sdl -m "URL1" "URL2" -d ./downloads -l 2.0 -j parallel
```

**Aggiunta di un file Gofile alla coda e successiva elaborazione:**
```bash
./sdl -a "https://gofile.io/d/abcdef"
./sdl -Q
```

**Download silenzioso (senza barre grafiche):**
```bash
./sdl -u "URL" --nobar
```

---
*Manuale generato automaticamente dall'analisi del codice di `sdl.c`.*
