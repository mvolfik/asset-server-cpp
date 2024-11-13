## Konfigurovatelný server pro nahrávání obrázků a generování responzivních variant

### Zadání

Server poskytuje HTTP endpoint, na který lze nahrávat obrázky. Server si z obrázku nejprve spočítá hash, aby zjistil, zda nejde o duplikát. Pokud ne, vytvoří si složku v cestě pro dočasné soubory, a zde originální soubor uloží. Poté vygeneruje responzivní varianty, jeden soubor pro každou kombinaci souborového formátu a velikosti (dle konfigurace). Toto by mělo probíhat co nejvíce paralelně, ale zároveň nepřehltit systém v případě více souběžných požadavků nebo generování velkého množství variant - tedy využití nějakého poolu vláken. Zároveň bychom se měli vyhnout zbytečné práci - např. pokud je škálování obrázku náročné, obrázek přeškálujeme na každou cílovou velikost pouze jednou, a z tohoto vygenerujeme jednotlivé varianty různých souborových formátů. Nakonec celou složku přesuneme na cílovou cestu, a odpovíme klientovi s informacemi o všech vygenerovaných variantách a cestě, do které byl soubor uložen.  
V postupu výše je navíc možná race condition: pokud paralelně dostaneme dva požadavky se stejným obrázkem, generovali bychom totéž dvakrát. Toto bychom měli také zachytit, a pro jeden požadavek obrázky vygenerovat, a v druhém pouze čekat na dokončení generování.

Dočasnou složku server používá proto, aby v každou chvíli byly všechny obrázky v datové složce v konzistentním stavu, a jiný server, který z ní bude číst, si tak nemohl přečíst a zacachovat nekompletní soubor.

Chování serveru bude možné přizpůsobit konfiguračním souborem (jehož parsování je také samozřejmě součást zápočtového projektu), který bude poskytovat přinejmenším následující možnosti:

- Cesty adresářů pro nahrané obrázky a pro dočasná data (měly by být na stejném filesystému, abychom mohli provádět atomické přesuny).
- Velikosti, do kterých se budou obrázky konvertovat. Tyto by mělo být možné specifikovat v procentech oproti původní velikosti i absolutní hodnotou v pixelech. Zde by bylo fajn mít nějakou obecně určenou posloupnost: např. _původní velikost, a pak opakovaně o 200px (nebo třeba 10 %) míň, až po minimální velikost 150px_.
- Souborové formáty, do kterých se budou jednotlivé varianty konvertovat.
  - Tato konfigurace by měla umožnit nějaké dynamické chování podle formátu původního souboru: např. vždy chci vygenerovat webp a jpg, a pokud je zdrojový obrázek v png nebo gif, tak chci zachovat i ten.
- Chování HTTP serveru: adresu a port, na které poslouchá, a nastavení CORS (tzn. ze kterých domén bude možné na server nahrávat přímo v prohlížeči).
- Autorizační token, který musí být obsažen v požadavcích. Toto chování by ale také mělo být možné vypnout.

Co tento projekt explicitně neřeší je přístup k nahraným obrázkům. K tomu je nejlepší použít nějaký mainstream HTTP server nastavený tak, aby odpovídal na požadavky z téže složky, do které tento server soubory ukládá. Pravděpodobně to bude umět daleko rychleji, než bych sám dokázal naprogramovat. Konkrétně lze použít např. Caddy, nginx nebo Apache, které mohou současně dělat i TLS termination a přeposílat požadavky na konkrétní cestu na tento uploader server. Součástí finální prezentace bude ukázka kompletního deploymentu.

Vzhledem k serverové povaze projektu bych se soustředil primárně na spuštění na Linuxových systémech, ale pokud nebude nějaký dobrý důvod bránící podpoře Windows (jmenovitě v tuto chvíli např. nevím, zda lze na Windows atomicky přesunout celou složku), projekt by měl fungovat i na této platformě.

Testy plánuji psát typu end-to-end: server spustím s nějakou konfigurací formátů nějakou dočasnou cestou pro ukládání, HTTP požadavkem nahraji obrázek nějaké známé velikosti, a očekávám že dostanu odpověď s velikostmi odpovídajícími zadané konfiguraci. Testovací skript bych psal asi v Pythonu.

Použité knihovny:

- ImageMagick (Magick++) pro konverzi obrázků mezi velikostmi a formáty
- HTTP server: [Boost.Beast](https://github.com/boostorg/beast)
