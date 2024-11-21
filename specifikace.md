## Konfigurovatelný server pro nahrávání obrázků a generování responzivních variant

Cílem projektu je vytvořit webový server, na který bude přes HTTP možné nahrávat obrazové soubory. Server bude z těchto souborů generovat varianty v menším rozlišení a tyto konvertovat do zadaných souborových formátů.

Zamýšlené využití je jako komponenta redakčního systému pro webové stránky: autor obsahu vybere soubor, který se nahraje na tento server.
Redakční systém tak získá k dispozici responzivní varianty, které může požději při prezentaci obsahu návštěvníkovi využít pro zobrazení obrázku na menších zařízeních ve vhodném rozlišení. Tím se zrychlí načítání stránek a sníží množství přenesených dat.

Konkrétní příklad:

1. Server je nakonfigurovaný na generování variant 300px a 800px na šířku + originál, vše ve formátech WebP a JPEG.
2. Uživatel nahraje soubor `obrazek.png` o rozměrech 1200px x 1500px. Hash obrázku je `0123456789abcdef`
3. Server uloží následující strukturu souborů:

```raw
data/
  0123456789abcdef/
    obrazek.png
    1200x1500/
      obrazek.webp
      obrazek.jpeg
    300x375/
      obrazek.webp
      obrazek.jpeg
    800x1000/
      obrazek.webp
      obrazek.jpeg
```

4. Server pošle následující odpověď:

```json
{
  "filename": "obrazek",
  "hash": "0123456789abcdef",
  "original": {
    "width": 1200,
    "height": 1500,
    "formats": ["png"]
  },
  "variants": [
    {
      "width": 1200,
      "height": 1500,
      "formats": ["webp", "jpeg"]
    },
    {
      "width": 300,
      "height": 375,
      "formats": ["webp", "jpeg"]
    },
    {
      "width": 800,
      "height": 1000,
      "formats": ["webp", "jpeg"]
    }
  ]
}
```

### Přibližný postup zpracování požadavku

Serveru na vystavený HTTP endpoint přijde požadavek. Z dat si nejprve spočítá hash, aby zjistil, zda nejde o již existující soubor. Pokud ne, vytvoří si složku v cestě pro dočasné soubory, a zde originální soubor uloží. Poté vygeneruje responzivní varianty, jeden soubor pro každou kombinaci souborového formátu a velikosti (dle konfigurace). Nakonec celou složku přesune na cílovou cestu, a odpoví klientovi s informacemi o všech vygenerovaných variantách a cestě, do které byl soubor uložen.

Toto by mělo probíhat co nejvíce paralelně, ale zároveň nepřehltit systém v případě více souběžných požadavků nebo generování velkého množství variant - tedy s využitím nějakého poolu vláken. Zároveň bychom se měli vyhnout zbytečné práci - např. pokud je škálování obrázku náročné, obrázek přeškálujeme na každou cílovou velikost pouze jednou, a z tohoto vygenerujeme jednotlivé varianty různých souborových formátů.

Dočasnou složku server používá proto, aby v každou chvíli byly všechny obrázky v datové složce v konzistentním stavu, a jiný server, který z ní bude číst, si tak nemohl přečíst a zacachovat nekompletní soubor.

### Konfigurace

Chování serveru bude možné přizpůsobit konfiguračním souborem (jehož parsování je také samozřejmě součást zápočtového projektu), který bude poskytovat přinejmenším následující možnosti:

- Cesty adresářů pro nahrané obrázky a pro dočasná data (měly by být na stejném filesystému, abychom mohli provádět atomické přesuny).
- Velikosti, do kterých se budou obrázky konvertovat. Tyto by mělo být možné specifikovat v procentech oproti původní velikosti i absolutní hodnotou v pixelech. Zde by bylo fajn mít nějakou obecně určenou posloupnost: např. _původní velikost, a pak opakovaně o 200px (nebo třeba 10 %) míň, až po minimální velikost 150px_.
- Souborové formáty, do kterých se budou jednotlivé varianty konvertovat.
  - Tato konfigurace by měla umožnit nějaké dynamické chování podle formátu původního souboru: např. vždy chci vygenerovat webp a jpeg, a pokud je zdrojový obrázek v png nebo gif, tak chci zachovat i ten.
- Chování HTTP serveru: adresu a port, na které poslouchá, a nastavení CORS (tzn. ze kterých domén bude možné na server nahrávat přímo v prohlížeči).
- Autorizační token, který musí být obsažen v požadavcích. Toto chování by ale také mělo být možné vypnout.

### Testování

Testy plánuji psát typu end-to-end: server spustím s nějakou konfigurací formátů nějakou dočasnou cestou pro ukládání, HTTP požadavkem nahraji obrázek nějaké známé velikosti, a očekávám že dostanu odpověď s velikostmi odpovídajícími zadané konfiguraci. Testovací skript bude napsaný pravděpodobně v Pythonu.

### Rozšiřitelnost

Kromě toho by měl být projekt dostatečně rozšiřitelný např. v následujících ohledech:

- Rozšíření možností konfigurovatelnosti

  Tzn. parsování konfiguračního souboru by mělo být dostatečně obecné a oddělené od "business logic". Toto přináší i další výhodu, a to možnost oddělené testovatelnosti parsování konfigurace.

- Přidání dalších HTTP endpointů pro další úkoly

  Byť tento projekt zapadá do microservices architektury a v mnoha ohledech by mělo být snadnější pro další komponenty vytvořit oddělenou aplikaci, z důvodu např. sdílené konfigurace může v budoucnosti být výhodné přidat další endpointy do této aplikace. Toto by mělo být snadno proveditelné s minimálními zásahy do existujícího kódu.

- Přidání dalších úkonů do zpracování přijatého souboru

  Časem mohou vzniknout požadavky na další úkony ve zpracování obrázku, např. přidání vodoznaku, detekce obličejů apod. Architektura fronty úkolů, která bude spravovat provádění dílčích operací ve vláknech (škálování velikosti, konverze formátu nebo např. nově přidaná detekce obličejů), které na sobě mohou navíc nějak souviset, by tedy měla být dostatečně flexibilní pro přidávání dalších kroků do libovolné fáze zpracování (na začátek, po přeškálování, po každé dílčí konverzi formátu, či po vygenerování všech variant).

### Další požadavky

Co tento projekt explicitně neřeší je přístup k nahraným obrázkům. K tomu je nejlepší použít nějaký mainstream HTTP server nastavený tak, aby odpovídal na požadavky z téže složky, do které tento server soubory ukládá. Pravděpodobně to bude umět daleko rychleji, než bych sám dokázal naprogramovat. Konkrétně lze použít např. Caddy, nginx nebo Apache, které mohou současně dělat i TLS termination a přeposílat požadavky na konkrétní cestu na tento uploader server. Součástí finální prezentace bude ukázka kompletního deploymentu.

Vzhledem k serverové povaze projektu se plánuji soustředit primárně na spuštění na Linuxových serverech, ale pokud nebude nějaký dobrý důvod bránící podpoře Windows (jmenovitě v tuto chvíli např. nevím, zda lze na Windows atomicky přesunout celou složku a jak složité bude použít potřebné knihovny, se kterými mám zatím zkušenosti výhradně z Linuxu), projekt by měl fungovat i na této platformě.

### Použité knihovny

- [libvips](https://github.com/libvips/libvips) pro konverzi obrázků mezi velikostmi a formáty
- HTTP server: [Boost.Beast](https://github.com/boostorg/beast)
