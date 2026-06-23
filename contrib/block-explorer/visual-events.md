# Eventi Visuali DESY — Block Explorer

## Panoramica

`desy_visual_rank(hash)` assegna un rank **0–99** a ogni blocco del 18 Dicembre, derivato deterministicamente dal block hash.

I **top 5 blocchi per rank** in ogni giornata del 18 Dicembre sono **visual event blocks** — meritano risalto visivo nell'explorer (glow, badge, evidenziazione).

## Architettura

```
┌──────────────┐   RPC 18081    ┌───────────────────┐   HTTP    ┌──────────────────┐
│  mevacoind   │ ◄──────────── │  desy_events.py   │ ────────► │  Block Explorer  │
│  (mainnet)   │               │  (script Python)  │  JSON     │  (frontend)      │
└──────────────┘               └───────────────────┘           └──────────────────┘
```

## Componenti

### 1. Script Python `desy_events.py`

Servizio leggero che:

- Ogni 10 secondi interroga `mevacoind` via RPC (`get_info` → nuova altezza)
- Per ogni nuovo blocco: `get_block <height>` → estrae hash → calcola `desy_visual_rank(hash)`
- Se oggi è 18 Dicembre: mantiene classifica ordinata dei top 5 blocchi
- Espone endpoint HTTP: `GET /events/top5`

### 2. Frontend Block Explorer

Legge l'endpoint e mostra:

| Elemento | Descrizione |
|----------|-------------|
| **Badge** | Icona stella/festa sul blocco nella timeline |
| **Glow** | Bagliore dorato attorno alla card del blocco |
| **Tooltip** | "DESY Visual Event — Rank 97/100" |
| **Filtro** | Checkbox "Mostra solo eventi DESY" |
| **Pagina evento** | `/block/<height>` con sezione "DESY Event" |

## Dettaglio endpoint

### `GET /events/top5`

```json
{
  "date": "2026-12-18",
  "events": [
    {"height": 512, "rank": 98, "hash": "abc...", "timestamp": 1767984000},
    {"height": 498, "rank": 91, "hash": "def...", "timestamp": 1767983000},
    ...
  ],
  "total_blocks": 150,
  "updated_at": "2026-12-18T12:00:00Z"
}
```

### `GET /events/check?height=512`

```json
{
  "height": 512,
  "hash": "abc...",
  "rank": 98,
  "is_visual_event": true,
  "position": 1
}
```

## Calcolo del rank (da desy.h)

```cpp
inline unsigned int desy_visual_rank(const crypto::hash& block_hash)
{
    uint64_t h;
    memcpy(&h, block_hash.data, sizeof(h));
    return static_cast<unsigned int>(h % 100);
}
```

Equivalente Python:

```python
def desy_visual_rank(block_hash_hex: str) -> int:
    h = int(block_hash_hex[:16], 16)  # primi 8 byte
    return h % 100
```

## Note implementative

- **Solo 18 Dicembre**: i blocchi fuori da questa data non hanno rank significativo
- **Persistenza**: salvare i top 5 su file JSON per evitare di ricalcolare a ogni riavvio
- **Cache**: RPC `get_block` può essere lento su blockchain grandi; usare `get_block_header_by_height` se serve solo hash
- **Fuso orario**: usare UTC per la data (mezzanotte UTC = inizio/fine evento)
- **RPC auth**: se il demone ha `--rpc-login`, lo script deve passare le credenziali

## Esempio di esecuzione

```bash
python3 desy_events.py \
  --rpc-url http://127.0.0.1:18081 \
  --rpc-login meva:desy \
  --http-port 8080 \
  --data-dir ~/.mevacoin/events
```

## Integrazione frontend (suggerimenti)

Esempio con React/Vue:

```tsx
// BlockCard.tsx
function BlockCard({ height, hash, ...props }) {
  const [event, setEvent] = useState(null);

  useEffect(() => {
    fetch(`http://events.local:8080/events/check?height=${height}`)
      .then(r => r.json())
      .then(setEvent);
  }, [height]);

  return (
    <div className={event?.is_visual_event ? 'block-glow' : ''}>
      {event?.is_visual_event && <Badge rank={event.rank} />}
      ...
    </div>
  );
}
```

```css
/* block-glow.css */
.block-glow {
  box-shadow: 0 0 20px rgba(255, 215, 0, 0.5);
  border: 2px solid gold;
}
```

## Roadmap

| Fase | Cosa | Quando |
|------|------|--------|
| 1 | Script Python base (polling + rank + API) | Ora |
| 2 | Integrazione frontend explorer | Dopo |
| 3 | Storico persistente (SQLite) | Opzionale |
| 4 | Webhook notifica evento (Telegram/Discord) | Opzionale |
