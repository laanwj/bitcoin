#!/usr/bin/env python3
# Copyright (c) 2014-2017 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test running bitcoind with the -rpcbind and -rpcallowip options."""

import sys

from test_framework.test_framework import BitcoinTestFramework, SkipTest
from test_framework.util import *
from test_framework.netutil import *

class RPCBindTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1

    def setup_network(self):
        self.add_nodes(self.num_nodes, None)

    def run_bind_test(self, allow_ips, connect_to, addresses, expected):
        '''
        Start a node with requested rpcallowip and rpcbind parameters,
        then try to connect, and check if the set of bound addresses
        matches the expected set.
        '''
        self.log.info("Bind test for %s" % str(addresses))
        expected = [(addr_to_hex(addr), port) for (addr, port) in expected]
        base_args = ['-disablewallet', '-nolisten']
        if allow_ips:
            base_args += ['-rpcallowip=' + x for x in allow_ips]
        binds = ['-rpcbind='+addr for addr in addresses]
        self.nodes[0].rpchost = connect_to
        self.start_node(0, base_args + binds)
        pid = self.nodes[0].process.pid
        assert_equal(set(get_bind_addrs(pid)), set(expected))
        self.stop_nodes()

    def run_allowip_test(self, allow_ips, rpchost, rpcport):
        '''
        Start a node with rpcallow IP, and request getnetworkinfo
        at a non-localhost IP.
        '''
        self.log.info("Allow IP test for %s:%d" % (rpchost, rpcport))
        base_args = ['-disablewallet', '-nolisten'] + ['-rpcallowip='+x for x in allow_ips]
        self.nodes[0].rpchost = None
        self.start_nodes([base_args])
        # connect to node through non-loopback interface
        node = get_rpc_proxy(rpc_url(get_datadir_path(self.options.tmpdir, 0), 0, "%s:%d" % (rpchost, rpcport)), 0, coveragedir=self.options.coveragedir,
                source_address=(rpchost, 0))
        node.getnetworkinfo()
        self.stop_nodes()

    def run_test(self):
        # due to OS-specific network stats queries, this test works only on Linux
        if not sys.platform.startswith('linux'):
            raise SkipTest("This test can only be run on linux.")
        have_ipv6 = test_ipv6_local()
        if not have_ipv6:
            self.log.warning("No IPv6 on this host, skipping tests that require IPv6")

        # use a second loopback IP for testing
        other_loopback_ip = '127.0.0.2'
        self.log.info("Using IP %s for testing" % other_loopback_ip)

        defaultport = rpc_port(0)

        if have_ipv6:
            # check default without rpcallowip (IPv4 and IPv6 localhost)
            self.run_bind_test(None, '127.0.0.1', [],
                [('127.0.0.1', defaultport), ('::1', defaultport)])
            # check default with rpcallowip (IPv6 any)
            self.run_bind_test(['127.0.0.1'], '127.0.0.1', [],
                [('::0', defaultport)])
        # check only IPv4 localhost (explicit)
        self.run_bind_test(['127.0.0.1'], '127.0.0.1', ['127.0.0.1'],
            [('127.0.0.1', defaultport)])
        # check only IPv4 localhost (explicit) with alternative port
        self.run_bind_test(['127.0.0.1'], '127.0.0.1:32171', ['127.0.0.1:32171'],
            [('127.0.0.1', 32171)])
        # check only IPv4 localhost (explicit) with multiple alternative ports on same host
        self.run_bind_test(['127.0.0.1'], '127.0.0.1:32171', ['127.0.0.1:32171', '127.0.0.1:32172'],
            [('127.0.0.1', 32171), ('127.0.0.1', 32172)])
        if have_ipv6:
            # check only IPv6 localhost (explicit)
            self.run_bind_test(['[::1]'], '[::1]', ['[::1]'],
                [('::1', defaultport)])
            # check both IPv4 and IPv6 localhost (explicit)
            self.run_bind_test(['127.0.0.1'], '127.0.0.1', ['127.0.0.1', '[::1]'],
                [('127.0.0.1', defaultport), ('::1', defaultport)])

        # check only non-loopback interface
        self.run_bind_test([other_loopback_ip], other_loopback_ip, [other_loopback_ip],
            [(other_loopback_ip, defaultport)])

        # Check that with invalid rpcallowip, we are denied
        self.run_allowip_test([other_loopback_ip], other_loopback_ip, defaultport)
        assert_raises_rpc_error(-342, "non-JSON HTTP response with '403 Forbidden' from server", self.run_allowip_test, ['1.1.1.1'], other_loopback_ip, defaultport)

if __name__ == '__main__':
    RPCBindTest().main()
