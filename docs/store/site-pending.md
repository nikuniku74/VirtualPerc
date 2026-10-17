# Sito: stato

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

## Collegare virtualpercussionist.it (da fare su register.it)
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
