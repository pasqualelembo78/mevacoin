// Copyright (c) 2024, The Mevacoin Project
// pool_distribution.cpp — On-chain pool distribution validation

#include "pool_distribution.h"
#include "pool_address.h"
#include "mevatrust_tx_parser.h"
#include "misc_log_ex.h"
#include <algorithm>

#undef MEVACOIN_DEFAULT_LOG_CATEGORY
#define MEVACOIN_DEFAULT_LOG_CATEGORY "mevatrust.pool_dist"

namespace cryptonote {
namespace mevatrust {

std::vector<std::pair<account_public_address, uint64_t>>
build_distribution_outputs(
    const std::vector<NodeCoinbaseReward>& rewards,
    uint64_t total_pool_balance)
{
    // Proportional split with pure integer arithmetic (deterministic across
    // platforms, unlike the float version).  The LAST qualifying reward absorbs
    // the rounding remainder so sum(outputs) == total_pool_balance exactly.
    std::vector<std::pair<account_public_address, uint64_t>> outputs;

    if (rewards.empty() || total_pool_balance == 0) return outputs;

    // Score is each reward's amount; skip zero-amount entries.
    std::vector<std::pair<account_public_address, uint64_t>> q;
    uint64_t total_score = 0;
    for (const auto& r : rewards) {
        if (r.amount == 0) continue;
        q.emplace_back(r.address, r.amount);
        total_score += r.amount;
    }
    if (q.empty()) return outputs;

    uint64_t allocated = 0;
    for (size_t i = 0; i < q.size(); ++i) {
        uint64_t share;
        if (i == q.size() - 1) {
            share = total_pool_balance > allocated ? total_pool_balance - allocated : 0;
        } else {
            share = (total_pool_balance * q[i].second) / total_score;
        }
        if (share > 0 || outputs.empty()) {
            outputs.emplace_back(q[i].first, share);
            allocated += share;
        }
    }

    return outputs;
}

bool construct_pool_distribution_extra(
    uint64_t height,
    uint32_t period,
    uint64_t total_pool_balance,
    const std::vector<NodeCoinbaseReward>& rewards,
    const frost::FrostSignature& signature,
    std::vector<uint8_t>& extra_out)
{
    auto outputs = build_distribution_outputs(rewards, total_pool_balance);
    if (outputs.empty()) {
        MWARNING("[PoolDist] No outputs to distribute");
        return false;
    }
    
    uint64_t total_distributed = 0;
    for (const auto& [addr, amt] : outputs) total_distributed += amt;
    
    if (total_distributed > total_pool_balance) {
        MERROR("[PoolDist] Distributed > pool balance");
        return false;
    }
    
    // Build distribution struct
    tx_extra_mevatrust_pool_distribution dist;
    dist.height = height;
    dist.period = period;
    dist.total_pool_balance = total_pool_balance;
    dist.total_distributed = total_distributed;
    // frost_R is stored as raw 32-byte point bytes; FrostSignature.R is a point.
    memcpy(&dist.frost_R, &signature.R, sizeof(dist.frost_R));
    dist.frost_z = signature.z;
    
    for (const auto& [addr, amt] : outputs) {
        dist.outputs.emplace_back(addr.m_spend_public_key, amt);
    }
    
    // Serialize to extra blob
    return mevatrust::build_mevatrust_pool_distribution_extra(dist, extra_out);
}

bool validate_pool_distribution(
    const tx_extra_mevatrust_pool_distribution& dist,
    uint64_t current_height,
    uint32_t current_period,
    uint64_t current_pool_balance,
    const ProposerState& proposers,
    std::string& error_msg)
{
    // 1. Period check
    if (dist.period != current_period) {
        error_msg = "Pool distribution period mismatch: tx=" + std::to_string(dist.period) +
                    " current=" + std::to_string(current_period);
        MERROR(error_msg);
        return false;
    }
    
    // 2. Height must be at period boundary
    if (dist.height < current_height) {
        error_msg = "Pool distribution from future height";
        MERROR(error_msg);
        return false;
    }
    
    // 3. Sum(outputs) <= pool_balance
    //
    // Tolto. Era un controllo che non poteva reggere, e che anzi poteva far
    // fermare la catena: diceva che non si distribuisce piu' di quanto il pool
    // contenga, ma la base della distribuzione NON e' il saldo del pool. E' il 3%
    // della ricompensa del blocco. Il saldo e' la somma dei 3% di tutti i blocchi
    // meno le distribuzioni passate, e puo' legittimamente essere minore del 3% di
    // un blocco solo: capita sui primi blocchi, e capita ogni volta che una
    // distribuzione passata ha gia' preso piu' di quanto era entrato.
    //
    // In quel caso il miner onesto produceva un blocco con il 3% corretto e i
    // validatori lo respingevano perche' il saldo non bastava — che vuol dire che il
    // miner non poteva produrre NULLA. Un validatore che rifiuta sempre non e' che
    // non tollera i soldi, e' che blocca.
    //
    // Quello che il consenso deve accertare e' che gli importi siano quelli che il
    // protocollo prescrive, e lo fa check_mevatrust_coinbase_pure_consensus(),
    // ricalcolandoli da zero. Un controllo che confronta il conto con se stesso non
    // aggiunge niente: aggiunge solo un modo di essere sbagliati.
    (void)current_pool_balance;
    
    // 4. Sum(outputs) must match individual outputs
    uint64_t output_sum = 0;
    for (const auto& [addr, amt] : dist.outputs) {
        output_sum += amt;
    }
    if (output_sum != dist.total_distributed) {
        error_msg = "Pool distribution output sum mismatch";
        MERROR(error_msg);
        return false;
    }
    
    // 5. La firma FROST non e' piu' un controllo di consenso.
    //
    // Un tempo questo passo accertava che il manifesto fosse firmato dal gruppo
    // dei cinque proposer. Accertava solo quello: che il documento fosse
    // autentico, non che fosse giusto. Cinque possessori di chiavi potevano
    // firmare un manifesto perfetto con dentro un destinatario a scelta loro, e
    // questa verifica lo avrebbe accettato: la firma diceva "sono stati loro", non
    // "e' corretto".
    //
    // Il controllo che chiude quel buco e' check_mevatrust_coinbase_pure_consensus(),
    // che ricalcola la lista dei destinatari dai dati di catena e la confronta voce
    // per voce. Non ha bisogno di fidarsi di nessuno, quindi non ha bisogno della
    // firma: e' una regola, non un permesso.
    //
    // Rimossa qui, non aggirata: se restasse, un blocco senza chiavi sarebbe
    // respinto per un documento che il consenso non usa piu', e il messaggio di
    // errore punterebbe a un file che nessuno sta piu' scrivendo.
    (void)proposers;  // resta il parametro: cambiare la firma romperebbe ogni chiamante
    
    MINFO("[PoolDist] Distribution validated: period=" << dist.period
          << " total=" << dist.total_distributed
          << " outputs=" << dist.outputs.size());
    return true;
}

} // namespace mevatrust
} // namespace cryptonote