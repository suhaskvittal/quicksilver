'''
    author: Suhas Vittal
    date:   2 April 2026
'''

from common import *
import numpy as np
import networkx as nx

#################################################################
#################################################################

def _translate_graph_into_qaoa_layer(G: nx.Graph, qreg: str) -> str:
    # generate two random angles
    a, b = np.random.random(2) * 2*np.pi

    # generate the QAOA circuit:
    out = ''
    for (u,v,w) in G.edges(data='weight'):
        out += str(GATE('rzz').arg(a*w).operand(qreg, u, v))
    for u in G.nodes():
        out += str(GATE('rx').arg(b).operand(qreg, u))
    return out

#################################################################
#################################################################

def build_graph_qaoa(output_file: str, G: nx.Graph, p: int):
    QR = 'q'

    with open(output_file, 'w') as wr:
        wr.write(f'''OPENQASM 2.0;
`include "qelib1.inc";
qreg {QR}[{G.number_of_nodes()}];

h {QR};
''')
        for i in range(p):
            if i % 10 == 0:
                print(f'QAOA iter {i} of {p}')
            wr.write(_translate_graph_into_qaoa_layer(G, QR))

#################################################################
#################################################################

if __name__ == '__main__':
    G_random = nx.erdos_renyi_graph(2048, 0.1)
    nx.set_edge_attributes(G_random, {e: np.random.random() for e in G_random.edges()}, 'weight')
    G_3reg = nx.random_regular_graph(3, 2048)
    nx.set_edge_attributes(G_3reg, {e: np.random.random() for e in G_3reg.edges()}, 'weight')
    G_powerlaw = nx.powerlaw_cluster_graph(2048, 8, 0.1)
    nx.set_edge_attributes(G_powerlaw, {e: np.random.random() for e in G_powerlaw.edges()}, 'weight')
    build_graph_qaoa('bisquit/qasm/qaoa_random.qasm', G_random, 200)
    build_graph_qaoa('bisquit/qasm/qaoa_3regular.qasm', G_3reg, 2000)
    build_graph_qaoa('bisquit/qasm/qaoa_powerlaw.qasm', G_powerlaw, 1000)

#################################################################
#################################################################

