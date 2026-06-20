# Fase 1: Circle Registry (LMDB) — mevacoin core

## Cosa fare esattamente

### 1. Creare `circle_registry.h`

Path: `src/cryptonote_core/mevatrust/circle_registry.h`

Struct `CircleEntry`:

```cpp
struct CircleEntry {
  crypto::hash circle_id;                      // H(name || admin_pubkey || nonce)
  std::string name;                            // nome univoco cerchia
  crypto::public_key admin_pubkey;             // admin (inizialmente = creator)
  std::vector<crypto::public_key> members;     // lista membri (admin incluso)
  uint64_t created_height;
  uint64_t created_timestamp;
  uint64_t updated_at;
  std::string metadata;                        // descrizione opzionale

  bool is_admin(const crypto::public_key& pk) const;
  bool has_member(const crypto::public_key& pk) const;
};
```

Class `CircleRegistry` (stesso pattern di `NodeRegistry`):

```cpp
class CircleRegistry {
public:
  CircleRegistry(const std::string& db_path);
  ~CircleRegistry();

  // CRUD
  crypto::hash create_circle(const std::string& name,
                             const crypto::public_key& admin_pubkey,
                             uint64_t height);
  bool add_member(const crypto::hash& circle_id, const crypto::public_key& member_pubkey);
  bool remove_member(const crypto::hash& circle_id, const crypto::public_key& member_pubkey);
  bool change_admin(const crypto::hash& circle_id, const crypto::public_key& new_admin);
  bool disband_circle(const crypto::hash& circle_id);

  // Query
  bool get_circle(const crypto::hash& circle_id, CircleEntry& out) const;
  bool get_circle_by_name(const std::string& name, CircleEntry& out) const;
  std::vector<CircleEntry> list_circles() const;
  std::vector<CircleEntry> get_circles_for_member(const crypto::public_key& pubkey) const;
  bool is_member(const crypto::hash& circle_id, const crypto::public_key& pubkey) const;
  bool name_exists(const std::string& name) const;

  // Persistence
  bool load_from_disk();
  bool save_to_disk() const;
  bool clear_all();

private:
  std::string db_path_;
  MDB_env* m_env_{nullptr};
  MDB_dbi  m_dbi_{0};

  std::map<std::string, CircleEntry> circles_;    // key = hex(circle_id)
  std::map<std::string, std::string> name_to_id_; // name → hex(circle_id)
  mutable std::mutex lock_;

  static crypto::hash compute_circle_id(const std::string& name,
                                        const crypto::public_key& admin,
                                        uint64_t nonce);
  static std::string hk(const crypto::hash& h);
  static std::string pack_entry(const CircleEntry& e);
  static bool unpack_entry(const void* data, size_t sz, CircleEntry& e);
  bool db_put(const CircleEntry& e);
  bool db_del(const crypto::hash& circle_id);
};
```

Forward declarations LMDB (stile node_registry.h):
```cpp
struct MDB_env;
typedef unsigned int MDB_dbi;
```

Include: `<string> <map> <vector> <cstdint> <mutex> "crypto/crypto.h" "cryptonote_basic/tx_extra.h"`

### 2. Creare `circle_registry.cpp`

Path: `src/cryptonote_core/mevatrust/circle_registry.cpp`

Stesso pattern LMDB di `node_registry.cpp`:

- `hk()`: `epee::string_tools::pod_to_hex(h)`
- `pack_entry()`: serializzazione binaria  
  Layout value:
  ```
  circle_id[32]
  name_len[2] + name
  admin_pubkey[32]
  member_count[4] + members[][32]
  created_height[8]
  created_timestamp[8]
  updated_at[8]
  metadata_len[2] + metadata
  ```
  Usare `lmdb_write_str()` / `lmdb_write_pod()` da `mevatrust_lmdb.h`
- `unpack_entry()`: deserializzazione speculare  
  Usare `lmdb_read_str()` / `lmdb_read_pod()`
- `db_put()`: stesso identico codice di `NodeRegistry::db_put()` ma con `m_dbi_`
- `db_del()`: stesso identico codice di `NodeRegistry::unregister_node()` per la parte LMDB
- `compute_circle_id()`: `cn_fast_hash(name + admin_pubkey + nonce)` — nonce = timestamp corrente
- `create_circle()`:
  1. `std::lock_guard<std::mutex> lk(lock_)`
  2. Verificare che `name` non sia vuoto e non > 32 char
  3. Verificare unicità nome: `name_exists()`
  4. `compute_circle_id()`
  5. Popolare `CircleEntry` con admin come primo membro
  6. `db_put()`
  7. Inserire in `circles_` e `name_to_id_`
  8. Log con `MINFO`
- `add_member()`: lock → lookup → se già membro return false → push → db_put → update_at
- `remove_member()`: lock → lookup → se è admin return false (deve cambiare admin prima) → remove → db_put
- `change_admin()`: lock → lookup → se il nuovo non è membro return false → set admin_pubkey → db_put
- `disband_circle()`: lock → lookup → db_del → erase da circles_ e name_to_id_
- `load_from_disk()`: cursor LMDB su `DB_MT_CIRCLES`, unpack ogni entry, popola `circles_` e `name_to_id_`
- `save_to_disk()`: drop table, re-put tutte le entries
- `name_exists()`: `name_to_id_.find(name) != name_to_id_.end()`
- `get_circles_for_member()`: itera circles_, filtra per `has_member(pubkey)`
- `is_member()`: lookup → `has_member(pubkey)`

Include:
```cpp
#include "circle_registry.h"
#include "mevatrust_lmdb.h"
#include "misc_log_ex.h"
#include "string_tools.h"
#include <ctime>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>
```

### 3. Modificare `mevatrust_lmdb.h`

In `src/cryptonote_core/mevatrust/mevatrust_lmdb.h`, dopo `DB_MT_WELCOME` (riga 58), aggiungere:

```cpp
static constexpr const char* DB_MT_CIRCLES   = "MT_CIRCLES";
```

Aggiornare anche il commento sopra (le tabelle) se presente, aggiungendo:
```
//   MT_CIRCLES  — CircleRegistry    key=circle_id[32]       value=packed entry
```

### 4. Modificare `mevatrust_manager.h`

In `src/cryptonote_core/mevatrust/mevatrust_manager.h`:

1. Aggiungere `#include "circle_registry.h"` dopo `#include "node_registry.h"` (riga 22)
2. Aggiungere membro privato dopo `m_node_registry` (riga 109):
   ```cpp
   std::shared_ptr<CircleRegistry>      m_circle_registry;
   ```
3. Aggiungere accessor dopo `node_registry()` (dopo riga 51):
   ```cpp
   std::shared_ptr<CircleRegistry> circle_registry() const { return m_circle_registry; }
   ```

### 5. Modificare `mevatrust_manager.cpp`

In `src/cryptonote_core/mevatrust/mevatrust_manager.cpp`:

**Nella `init()`** (dopo `m_node_registry->load_from_disk()` alla riga 57, prima del blocco catch):

```cpp
m_circle_registry = std::make_shared<CircleRegistry>(p+"/db");
m_circle_registry->load_from_disk();
MINFO("[MevaTrustManager] CircleRegistry inizializzato");
```

**Nella `shutdown()`** (alla fine, dopo badge_system riga 138):

```cpp
if (m_circle_registry) m_circle_registry->save_to_disk();
```

### 6. Modificare `CMakeLists.txt`

In `src/cryptonote_core/CMakeLists.txt`, aggiungere nella lista `cryptonote_core_sources`:

```
  mevatrust/circle_registry.cpp
```

(posizionarlo dopo `mevatrust/node_registry.cpp` alla riga 36, ordine alfabetico)

## Verifica compilazione

```bash
cd /root/mevacoin && make -j$(nproc) 2>&1 | tail -40
```

Se fallisce solo per `circle_registry.cpp`, leggere l'errore e correggere.

## Pattern di riferimento

Leggere SEMPRE questi file prima di scrivere codice:
- `node_registry.h` — per forward declarations LMDB, stile classe
- `node_registry.cpp` — per `pack_entry/unpack_entry/db_put/db_del/load_from_disk/save_to_disk`
- `mevatrust_lmdb.h` — per helper `lmdb_write_str/lmdb_read_str/lmdb_write_pod/lmdb_read_pod`

## Cosa NON fare (lo faremo in fasi successive)

- NON toccare `tx_extra.h` (tag 0xA3 sarà Fase 2)
- NON toccare `mevatrust_tx_parser.h/.cpp` (Fase 2)
- NON toccare RPC endpoints (Fase 3)
- NON toccare wallet GUI

## Cosa dire alla prossima sessione di opencode

Aprire opencode nella directory `/root/mevacoin/` e dire:

> "Implementa Fase 1 Circle Registry. Leggi il piano in `.opencode/plans/circle-registry-fase1.md` e seguilo. Devi creare `circle_registry.h`, `circle_registry.cpp`, e modificare `mevatrust_lmdb.h`, `mevatrust_manager.h/.cpp`, `CMakeLists.txt`. Poi verifica compilazione con `make`."
