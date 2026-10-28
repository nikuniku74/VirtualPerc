# Sito: stato

**Sito pubblicato e HTTPS verificato (2026-10-26).** Aggiornamenti IT/EN e nuovo
marchio pubblicati nel repository separato del sito: `f2c1fa2`, poi `ae9a855`.
Allineamento successivo IT/EN: `77d2bff` (guida, assistenza, privacy e titoli
localizzati), `27e77e8` (immagine social bilingue).
Logo trasparente, icone e immagine social pubblici coincidono con gli originali
in `site/`. Nessun invio App Store effettuato.

## Aggiornamento 2026-10-26

Dominio configurato su GitHub Pages prima della modifica DNS. I due nameserver
autorevoli e il resolver locale restituiscono i quattro A `185.199.108.153`,
`185.199.109.153`, `185.199.110.153`, `185.199.111.153`; `www` punta a
`nikuniku74.github.io`. Record di posta e altri servizi conservati.

HTTPS verificato con la catena di fiducia del sistema su `/`, `/privacy/`,
`/support/`, `/guide/`: HTTP200. `www` rimanda al dominio canonico in HTTPS.
GitHub Pages dichiara certificato approvato per entrambi i nomi, scadenza
2027-01-24, ed `https_enforced=true`. La generazione rimasta bloccata è stata
riavviata rimuovendo e ripristinando subito il dominio personalizzato, secondo
la procedura ufficiale GitHub; record DNS e posta conservati.

Le prove DNS, il controllo dei nove URL pubblici e la cattura del sito sono in
`build-host/vp-release-20261026/`: `site-public-https-check.json` e
`site-public-https.png`. Il browser apre il sito senza avvisi di sicurezza.

Il collegamento App Store `id6823639065` restituisce HTTP404 e la pagina Apple
conferma che non è disponibile. Su scelta dell'utente, tutte le pagine pubblicate
mostrano **In arrivo / Coming soon**, senza collegamenti di download attivi.
Ripristinare il collegamento soltanto dopo aver verificato la scheda pubblicata.

## Storico del 2026-10-16 (superato dall'aggiornamento sopra)

**Pubblicato il 2026-10-16** su GitHub Pages:

- Repository pubblico: <https://github.com/nikuniku74/virtualpercussionist-site>
- Indirizzo che risponde oggi: <https://nikuniku74.github.io/virtualpercussionist-site/>
  (pagine: `/`, `/privacy/`, `/support/`, `/guide/`).
- Dati mostrati: **Nicola Marogna** e **info@ritmodigitale.com**. **Nessun
  indirizzo postale** (lo script lo toglie se è vuoto): la privacy lo cita solo
  se lo aggiungi.
- Dominio scelto: **virtualpercussionist.it** (registrato presso register.it,
  nameserver `ns1/ns2.register.it`). **Il DNS non è ancora collegato a GitHub**:
  vedi i passi in fondo. Nell'app `kPrivacyUrl` è già
  `https://virtualpercussionist.it/privacy/`.

## Come aggiornare il sito
```bash
./scripts/build-site.sh /tmp/site "Nicola Marogna" "" "info@ritmodigitale.com"
# copia il contenuto di /tmp/site nel clone del repository del sito, commit e push
```
Il repository del sito è separato da questo (pubblico), perché le Pages gratuite
vogliono un repository pubblico.

## Procedura storica di collegamento (già eseguita)

Ordine corretto: configurare prima il dominio personalizzato nelle impostazioni
Pages del repository, poi modificare i DNS; attivare HTTPS quando il certificato
è disponibile. Non ripetere le modifiche seguenti sui record già aggiornati.
1. Nel pannello DNS del dominio, **sostituisci** il record `A` di `@`
   (oggi `195.110.124.133`) con questi quattro `A`: `185.199.108.153`,
   `185.199.109.153`, `185.199.110.153`, `185.199.111.153`
   (facoltativi i quattro `AAAA`: `2606:50c0:8000::153`, `...8001::153`,
   `...8002::153`, `...8003::153`).
2. Il record `www` (oggi un CNAME verso il dominio stesso) diventa un `CNAME`
   verso `nikuniku74.github.io` (senza il nome del repository).
3. **Non toccare** il record `MX` (`mail.register.it`): è la posta.
4. Attendi la propagazione (fino a 24 ore; spesso minuti). Controllo:
   `dig +short A virtualpercussionist.it` deve dare i quattro indirizzi GitHub.
5. Poi: impostare il dominio sul repository
   (`gh api -X PUT repos/nikuniku74/virtualpercussionist-site/pages -f cname=virtualpercussionist.it`)
   e, quando compare, attivare **Enforce HTTPS** (il certificato può richiedere
   fino a 24 ore). Da quel momento anche l'indirizzo `github.io` rimanda al
   dominio.


## FAQ e limiti —27/10/2026

Pubblicata `/faq/` con18 domande corrispondenti per lingua, italiano e inglese,
commit `a914b5e`. Riepilogo dei limiti in apertura e risposte espandibili native,
utilizzabili da tastiera. Collegata da header/footer di tutte le pagine e da
un richiamo in home prima dei piani. Sorgente canonica: `site/faq/index.html`.

Dichiarati: aggancio non immediato/infallibile; interruzioni protettive anche su
musica apparentemente regolare (segnalazione BRANO ancora in verifica); BPM
corretto senza garanzia di fase/primo battere; ambiguità metà/doppio; pause e
cambi dopo silenzio che possono richiedere STOP/START; limiti microfono,
ritardo/routing/buffer; metri supportati; differenza range manuale/AUTO;
accesso Pro, privacy e fallimento AI esplicito. Le misure Mac non certificano
da sole tenuta termica o lunghe sessioni su ogni iPad. Nessuna percentuale di
precisione o promessa di risoluzione non verificata.

60 controlli responsive/lingua su5 pagine e6 larghezze,18 domande identificate
per lingua, link/frammenti locali eID unici validati; apertura/chiusura da
tastiera IT/EN verificate. Sei file pubblici coincidono con quelli controllati,
HTTPS valido. Evidenze in `build-host/vp-faq-20261027/`. Motore audio invariato.
