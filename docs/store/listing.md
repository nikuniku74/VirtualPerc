# Scheda dello store (bozza da incollare in App Store Connect)

Segnaposto da riempire: `[EMAIL]`, `[SITO]`, `[NOME LEGALE / INDIRIZZO]`. Limiti
indicati da Apple: nome 30, sottotitolo 30, testo promozionale 170, parole
chiave 100 (separate da virgole, senza spazi), descrizione 4000.

## Informazioni generali

| campo | valore |
|---|---|
| Nome | Virtual Percussionist |
| Categoria principale | Musica |
| Categoria secondaria | Produttività (o Utilità) |
| Lingua principale | Inglese (U.S.), più Italiano |
| Classificazione età | 4+ (nessun contenuto sensibile; risposte «Nessuno» a tutto) |
| Prezzo app | Gratis, con abbonamento |
| Copyright | © 2026 [NOME LEGALE] |
| URL assistenza | [SITO]/support |
| URL privacy | [SITO]/privacy |
| URL marketing (facoltativo) | [SITO] |
| Disponibilità | iPhone, iPad, Mac (Apple silicon); **Apple Vision: no** |
| Scheda privacy | Dati non raccolti |

## English (U.S.)

**Subtitle** (28): `A percussionist that listens`

**Promotional text** (<=170):
`Try it free for 7 days. Virtual Percussionist follows your band's tempo in real time and plays shaker, congas and more, in the groove, from the first beat.`

**Keywords** (<=100):
`percussion,shaker,congas,drummer,band,tempo,beat,rhythm,live,musician,groove,backing,tambourine`

**Description**

```
A percussionist that listens to your band and plays along, in time.

Virtual Percussionist follows the tempo of what you play - live, from a mixer feed, from the microphone, or from a track you load - and adds shaker, congas, clap, tambourine and triangle in the right groove. When the band speeds up or slows down, it follows. When the band stops, it stops.

MADE FOR PLAYING LIVE
- Finds the tempo and the first beat of the bar by itself and keeps them, without a click track.
- One big START / STOP, a tap tempo, and a few knobs you can reach in the dark.
- Works on a mixer feed (USB class-compliant interfaces), the microphone, or a loaded track.
- Keeps playing when the screen locks; mixes with other apps instead of silencing them.

A PART THAT SOUNDS PLAYED
- Several styles, with the accents and ghost notes a real player would use.
- Swing, energy and reverb to sit the part in your mix.
- Follows small tempo changes, but does not panic on a loud fill.

PRIVATE BY DESIGN
Everything is analysed on your device. Audio is never recorded, uploaded or shared, and the app has no account.

SUBSCRIPTION
Virtual Percussionist Pro is an auto-renewing subscription (monthly or yearly) with a 7-day free trial for new subscribers. Payment is charged to your Apple ID at confirmation of purchase, or when the free trial ends. It renews automatically unless cancelled at least 24 hours before the end of the current period. Manage or cancel anytime in Settings > Apple ID > Subscriptions.
Terms of Use: https://www.apple.com/legal/internet-services/itunes/dev/stdeula/
Privacy Policy: [SITO]/privacy
```

**What's new (1.0)**: `First release.`

## Italiano

**Sottotitolo** (29): `Un percussionista che ascolta`

**Testo promozionale** (<=170):
`Provalo gratis per 7 giorni. Virtual Percussionist segue il tempo della tua band in tempo reale e suona shaker, conga e altro, nel groove giusto, dal primo battere.`

**Parole chiave** (<=100):
`percussioni,shaker,conga,batterista,band,tempo,ritmo,live,musicista,groove,accompagnamento`

**Descrizione**

```
Un percussionista che ascolta la tua band e suona insieme a lei, a tempo.

Virtual Percussionist segue il tempo di quello che suoni - dal vivo, dal mix del mixer, dal microfono o da un brano che carichi - e aggiunge shaker, conga, clap, tamburello e triangolo nel groove giusto. Se la band accelera o rallenta, lo segue. Se la band si ferma, si ferma.

PENSATO PER SUONARE DAL VIVO
- Trova da solo il tempo e il primo battere della battuta e li mantiene, senza metronomo.
- Un grande START / STOP, il tap tempo e pochi controlli raggiungibili al buio.
- Funziona con il mix del mixer (interfacce USB class-compliant), con il microfono o con un brano caricato.
- Continua a suonare a schermo bloccato; si mescola con le altre app invece di zittirle.

UNA PARTE CHE SUONA SUONATA
- Più stili, con gli accenti e le note fantasma che userebbe un musicista vero.
- Swing, energia e riverbero per inserire la parte nel tuo mix.
- Segue le piccole variazioni di tempo, ma non si fa prendere dal panico da un fill forte.

PRIVATO PER SCELTA
Tutto viene analizzato sul tuo dispositivo. L'audio non viene mai registrato, caricato o condiviso, e l'app non ha account.

ABBONAMENTO
Virtual Percussionist Pro e' un abbonamento a rinnovo automatico (mensile o annuale) con 7 giorni di prova gratuita per i nuovi abbonati. Il pagamento viene addebitato sul tuo ID Apple alla conferma dell'acquisto, o al termine della prova gratuita. Si rinnova automaticamente, a meno che non venga annullato almeno 24 ore prima della fine del periodo in corso. Puoi gestirlo o annullarlo in qualsiasi momento da Impostazioni > ID Apple > Abbonamenti.
Condizioni d'uso: https://www.apple.com/legal/internet-services/itunes/dev/stdeula/
Informativa privacy: [SITO]/privacy
```

**Novita' (1.0)**: `Prima versione.`

## Abbonamenti (App Store Connect → Abbonamenti)

Gruppo: **Virtual Percussionist Pro** (nome visibile al cliente: `Virtual Percussionist Pro`).

| ID | durata | nome (EN / IT) | descrizione (EN / IT) | prezzo proposto |
|---|---|---|---|---|
| `com.virtualpercussionist.app.pro.monthly` | 1 mese | Pro Monthly / Pro Mensile | Full access, billed monthly / Accesso completo, addebito mensile | 4,99 € |
| `com.virtualpercussionist.app.pro.yearly` | 1 anno | Pro Yearly / Pro Annuale | Full access, billed yearly / Accesso completo, addebito annuale | 34,99 € |

Offerta introduttiva su entrambi: **Gratis, 1 settimana, tutti i paesi**, nuovi
abbonati. Gerarchia nel gruppo: annuale sopra il mensile. **Periodo di
tolleranza di fatturazione: spento** (vedi `docs/APPSTORE.md` §4).
Screenshot di revisione per ciascun prodotto: `docs/paywall/en-1-offer-trial-phone.png`.

## Note per la revisione

```
Virtual Percussionist is a live percussion companion: it listens to a band (microphone, line input from a mixer, or a track loaded in the app) and plays shaker, congas and other percussion in time.

To try it without a band: open the app, tap SETUP, then PLAY in the INPUT section. With nothing loaded, PLAY loads a built-in demo song (about 35 seconds) and the percussion follows it; tap START on the main screen to hear the part. The microphone is only needed for live use; the demo needs no permission.

The microphone is analysed on the device only; nothing is recorded or sent. Background audio is used so a live set keeps playing when the iPad locks.

Subscription: 7-day free trial, then monthly or yearly (group "Virtual Percussionist Pro"). The offer opens when START or PLAY is pressed without an active subscription, and from SETUP > PRO. Restore Purchases, Terms of Use, Privacy Policy and Licenses are on that page. No account is required.
```

> Da verificare prima dell'invio: che davvero «SETUP → PLAY» carichi il brano e
> che START lo faccia suonare, sul dispositivo, in Release e con un acquisto di
> prova. Il testo qui sopra descrive come *dovrebbe* funzionare.
