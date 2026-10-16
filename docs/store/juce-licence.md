# Licenza JUCE per l'App Store

Letta il 2026-10-16 da <https://juce.com/get-juce/> e dall'EULA di JUCE 9
(<https://juce.com/legal/juce-9-licence/>, pubblicata il 17 giugno 2026).
**Mia lettura, non un parere legale**; per dubbi sul testo l'EULA stessa manda a
sales@juce.com. Il repo usa JUCE 8.0.15: secondo le FAQ di JUCE «una licenza
JUCE 9 dà il diritto di usare tutte le versioni precedenti», quindi basta una
licenza JUCE 9.

## Perché serve

JUCE è sotto licenza doppia: **AGPLv3 oppure licenza JUCE**. Con l'AGPL, chi
distribuisce un programma che contiene JUCE deve dare il codice sorgente a chi
lo riceve: non è compatibile con un'app chiusa sull'App Store. Per distribuire
codice chiuso serve una licenza JUCE per tutto il tempo in cui lo si distribuisce.

## I livelli (EULA §1.2)

| livello | ricavi annui ammessi | prezzo | note |
|---|---|---|---|
| **Starter** | fino a **20.000 $** | gratis, perpetua | tutte le funzioni |
| **Indie** | fino a **300.000 $** | **40 $/mese** per utente (minimo 1 mese), oppure **800 $ una tantum** | con l'abbonamento, per continuare a distribuire va tenuto attivo; la perpetua no |
| **Pro** | nessun limite | 175 $/mese (minimo 12 mesi) o 3.500 $ una tantum | |

Lo splash «Made with JUCE» non compare nell'EULA (zero occorrenze della
parola): `JUCE_DISPLAY_SPLASH_SCREEN=0` è già impostato nel progetto.

## Come si conta il limite di ricavi (§1.2.1) - il punto che decide il livello

- Se la licenza è di **una persona** (o di un'attività senza personalità
  giuridica separata): conta il totale dei ricavi che quella persona ottiene
  **dall'uso di JUCE**, da tutte le fonti (anche donazioni, pubblicità,
  indiretti), negli ultimi 12 mesi.
- Se la licenza è di **una società**: conta **tutto il fatturato della società e
  delle sue affiliate**, *anche quello che non ha niente a che fare con JUCE*,
  senza detrazioni.

Quindi: Starter va bene solo se l'intestatario è una persona e i ricavi
dell'app (più ogni altro ricavo da uso di JUCE) restano sotto 20.000 $ l'anno.
Se invece pubblichi tramite una società con un fatturato più alto, serve
almeno **Indie**. L'EULA vieta anche di usare due livelli insieme sullo stesso
prodotto.

## Cosa consiglio

- Se pubblichi a nome tuo e prevedi pochi ricavi il primo anno: **Starter**
  (gratis), e passi a Indie quando ti avvicini al limite.
- Se pubblichi tramite una società, o vuoi stare tranquillo: **Indie
  perpetua, 800 $**. L'abbonamento da 40 $/mese costa meno all'inizio ma va
  tenuto attivo finché l'app è in vendita (altrimenti, dice l'EULA, si deve
  smettere di distribuire).
- Quando il ricavo supera 300.000 $: Pro.

## Decisione (2026-10-16)

Si pubblica **a nome proprio (persona fisica)**: livello **Starter**, gratis.
Condizione: i ricavi che derivano dall'uso di JUCE (l'app, più ogni altra cosa
fatta con JUCE) negli ultimi 12 mesi restano **sotto 20.000 $**. **Da rivedere a
15.000 $**: prima di superare il limite si passa a Indie perpetua (800 $),
perché oltre il limite lo Starter non è più coperto. L'account sviluppatore
Apple e l'intestatario della licenza JUCE devono essere la stessa persona.

## Cosa fare (io non posso: è un acquisto e un contratto a tuo nome)

1. Decidi chi è l'intestatario (te o la società) e il livello.
2. Crea l'account su juce.com, scegli la licenza e **accetta l'EULA**.
3. Conserva la ricevuta/fattura e la schermata della licenza (fuori dal repo).
4. Scrivi qui sotto data, livello e intestatario, così il repo racconta com'è
   coperto:

```
Licenza JUCE: [Starter | Indie perpetua | Indie abbonamento | Pro]
Intestatario: [NOME / SOCIETA']
Data: [AAAA-MM-GG]   Numero ordine: [____]
```

5. Per le società: verifica il fatturato (anche quello non legato all'app)
   contro il limite del livello scelto.

Non ho trovato nell'EULA nulla da cambiare nel codice (nessun codice di
attivazione da inserire). Se JUCE 9 ne chiede uno all'acquisto, dimmelo e lo
cabliamo.
