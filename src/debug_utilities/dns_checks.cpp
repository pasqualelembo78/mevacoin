// Copyright (c) 2019-2024, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <boost/program_options.hpp>
#include "misc_log_ex.h"
#include "common/util.h"
#include "common/command_line.h"
#include "common/dns_utils.h"
#include "version.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <cstring>
#include <chrono>

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "debugtools.dnschecks"

namespace po = boost::program_options;

enum lookup_t { LOOKUP_A, LOOKUP_TXT };

/**
 * Prova a connettersi TCP a ip:port con timeout (ms).
 * Restituisce true se la connessione è stata stabilita.
 */
static bool probe_ip_port(const std::string &ip, int port, int timeout_ms = 2000)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    MWARNING("socket() failed: " << strerror(errno));
    return false;
  }

  // set non-blocking
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0)
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1)
  {
    close(fd);
    MWARNING("Invalid IP format: " << ip);
    return false;
  }

  int rc = connect(fd, (sockaddr*)&addr, sizeof(addr));
  if (rc == 0)
  {
    // immediate success
    close(fd);
    return true;
  }
  else if (errno != EINPROGRESS && errno != EWOULDBLOCK)
  {
    // immediate failure
    close(fd);
    return false;
  }

  // wait with poll
  pollfd pfd;
  pfd.fd = fd;
  pfd.events = POLLOUT;
  int pres = poll(&pfd, 1, timeout_ms);
  if (pres <= 0)
  {
    close(fd);
    return false;
  }

  // check for socket error
  int so_error = 0;
  socklen_t len = sizeof(so_error);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0)
  {
    close(fd);
    return false;
  }
  close(fd);
  return so_error == 0;
}

/**
 * Esegue un controllo semplice su una lista di IP (porta P2P)
 * e logga i risultati.
 */
static void check_seed_ips(const std::vector<std::string> &ips, int port = 18080)
{
  size_t reachable = 0;
  for (const auto &ip : ips)
  {
    if (probe_ip_port(ip, port, 2000))
    {
      MINFO("Seed IP reachable: " << ip << ":" << port);
      ++reachable;
    }
    else
    {
      MWARNING("Seed IP NOT reachable: " << ip << ":" << port);
    }
  }
  if (reachable == ips.size())
    MINFO(reachable << "/" << ips.size() << " seed IPs reachable");
  else
    MERROR(reachable << "/" << ips.size() << " seed IPs reachable");
}

static std::vector<std::string> lookup(lookup_t type, const char *hostname)
{
  // Questa utility è stata adattata per usare controlli IP diretti.
  // Manteniamo la funzione solo per compatibilità, ma in questa versione
  // non la useremo per i TXT DNS perché abbiamo seed IP fissi.
  return {};
}

static void lookup(lookup_t type, const std::vector<std::string> hostnames)
{
  // non usata nella variante IP-direct
  (void)type;
  (void)hostnames;
}

int main(int argc, char* argv[])
{
  TRY_ENTRY();

  tools::on_startup();

  po::options_description desc_cmd_only("Command line options");
  po::options_description desc_cmd_sett("Command line options and settings options");

  command_line::add_arg(desc_cmd_only, command_line::arg_help);

  po::options_description desc_options("Allowed options");
  desc_options.add(desc_cmd_only).add(desc_cmd_sett);

  po::variables_map vm;
  bool r = command_line::handle_error_helper(desc_options, [&]()
  {
    po::store(po::parse_command_line(argc, argv, desc_options), vm);
    po::notify(vm);
    return true;
  });
  if (! r)
    return 1;

  if (command_line::get_arg(vm, command_line::arg_help))
  {
    std::cout << "Monero '" << MONERO_RELEASE_NAME << "' (v" << MONERO_VERSION_FULL << ")" << ENDL << ENDL;
    std::cout << desc_options << std::endl;
    return 1;
  }

  mlog_configure("", true);
  mlog_set_categories("+" MONERO_DEFAULT_LOG_CATEGORY ":INFO");

  // --- Qui: elenco IP seed diretti per Mevacoin (porta P2P)
  std::vector<std::string> seed_ips = {
    
    "87.106.40.193"
  };

  // Verifica reachability su porta 18080 (P2P)
  check_seed_ips(seed_ips, 18080);

  // Se vuoi aggiungere altri controlli (es. porte RPC), aggiungi qui, ad esempio:
  // check_seed_ips(seed_ips, 18081); // controllo porta RPC (se esposta)

  return 0;
  CATCH_ENTRY_L0("main", 1);
}