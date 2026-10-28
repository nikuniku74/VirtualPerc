# Virtual Percussionist — identità e copertine

Identità aggiornata al 26 ottobre 2026: vetro e cromo fucsia, fondo prugna
scuro, marchio circolare e indicatore luminoso del battito.

## File definitivi

- `logo-1024.png`: marchio e scritta, PNG RGBA realmente trasparente.
- `mark-transparent-master.png`: simbolo isolato usato per tutte le derivazioni.
- `icon-1024.png`: icona Apple RGB senza alfa, con sfondo pieno coordinato.
- `mark-128.png` e `favicon-64.png`: simboli con trasparenza.
- `apple-touch-180.png`: icona web per iOS.
- `og-1200x630.png`: immagine di condivisione del sito.
- `features.html`: composizione delle32 schermate successive, con titoli IT/EN.
- `rhythm-orbit-v1.png`: fondo artistico delle copertine.

Le copertine sono `0-launch-cover.png` nelle cartelle IT/EN sotto
`docs/store/screenshots/`: iPhone 6,3" 1206×2622, iPhone 6,9" 1320×2868,
iPad 13" 2064×2752, Mac Designed for iPad 2880×1800. Caricare la copertina
come prima immagine, seguita dalla console e dalle altre schermate.

Sono composizioni promozionali con la vera interfaccia JUCE dell'app:
l'interfaccia non è stata ridisegnata dall'AI. Le immagini Mac mostrano
l'interfaccia iPad in una composizione, senza simulare una cattura di macOS.
Tutte le copertine sono RGB senza alfa. Le immagini di acquisto usano fixture
locali di prezzo e stato: non documentano una transazione reale.

Prima del caricamento verificare il formato dei dati, non solo l’estensione:
le catture browser possono essere JPEG anche se salvate con nome `.png`.
Il 27/10/2026 i 40 export Store sono stati ricodificati con `sips -s format png`
e controllati come PNG reali RGB, senza alfa e senza variazioni di dimensione;
il semplice cambio del nome non basta. Dopo l’upload attendere la validazione
Apple: miniature, contatori e salvataggio della pagina non provano che un file
sia stato elaborato correttamente.

## Riproduzione

Il fondo e il simbolo sono stati creati con **ImageGen integrato**, senza CLI
OpenAI o chiavi API. I prompt sono conservati in `prompts.json`.
La scritta viene composta nativamente, per conservarne la nitidezza e
ottenere una vera trasparenza, senza quadrati neri:

```sh
swift scripts/export-brand.swift docs/store/launch-art/mark-transparent-master.png docs/store/launch-art
```

`cover.html` compone fondo, marchio e schermate originali; i parametri sono
`device=phone63|phone69|tablet|mac|og` e `lang=it|en`. Le misure e i controlli
sono in `cover-exports.json` e `typography-check.json`.

`features.html` usa gli stessi parametri per phone63/phone69/tablet e
`shot=1-console|2-trial-offer|3-settings|4-subscription-status|5-guide|6-fixed-tempo`.
Entrambi i template leggono gli originali da `../native-screenshots/`, evitando
di inserire una composizione promozionale dentro un'altra. `feature-exports.json`
registra32 controlli su titoli e margini. L'immagine di condivisione del sito
è bilingue nello stesso file: «IL TUO GROOVE. / YOUR GROOVE.».

I file dell'app sono `Assets/AppIcon.png` e `Assets/Brand/LaunchLogo.png`.
La splash nativa mantiene i quattro pallini animati e la dissolvenza finale.
`VPShots --splash <cartella>` cattura le sue fasi reali su iPhone e iPad.

Le immagini Store sono escluse localmente da Git: conservare questi file
insieme al pacchetto della release. Non eliminare i master durante la pulizia
dei tracciati di prova.
