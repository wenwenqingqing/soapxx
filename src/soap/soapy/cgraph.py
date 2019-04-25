import numpy as np
import soap
import scipy.stats
import time
import momo
log = momo.osio
dtype = "float64"

def set_params(node, vals):
    v = np.array(vals)
    node.params().set(v, str(v.dtype))
    return

def translate_fgraph(fgraph):
    cgraph = soap.CGraph()
    node_map = {}
    for fidx, f in enumerate(fgraph):
        log << log.back << "Converting node %d/%d" % (
            fidx+1, len(fgraph)) << log.flush
        op = f.op_tag
        inputs = [ node_map[p.expr] for p in f.getParents() ]
        if f.is_root:
            c1 = cgraph.addInput()
            c2 = cgraph.addNode("linear", [c1])
            c2.setParamsConstant(True)
            set_params(c2, [1.0*f.prefactor*f.unit_prefactor,0.0])
            node_map[f.expr] = c2
        elif f.op_tag == "e":
            c1 = cgraph.addNode("linear", inputs)
            c2 = cgraph.addNode("exp", [c1])
            set_params(c1, [1.0,0.0])
            set_params(c2, [1.0])
            node_map[f.expr] = c2
        elif f.op_tag == "l":
            c1 = cgraph.addNode("linear", inputs)
            c2 = cgraph.addNode("log", [c1])
            set_params(c1, [1.0,0.0])
            node_map[f.expr] = c2
        elif f.op_tag == "|":
            c1 = cgraph.addNode("linear", inputs)
            c2 = cgraph.addNode("mod", [c1])
            set_params(c1, [1.0,0.0])
            node_map[f.expr] = c2
        elif f.op_tag == "s":
            c1 = cgraph.addNode("linear", inputs)
            c2 = cgraph.addNode("mod", [c1])
            c3 = cgraph.addNode("pow", [c2])
            set_params(c1, [1.0,0.0])
            set_params(c3, [0.5])
            node_map[f.expr] = c3
        elif f.op_tag == "r":
            c1 = cgraph.addNode("linear", inputs)
            c2 = cgraph.addNode("pow", [c1])
            # Cannot optimize exponent due to 
            # potentially negative inputs, hence:
            c2.setParamsConstant(True)
            set_params(c1, [1.0,0.0])
            set_params(c2, [-1.])
            node_map[f.expr] = c2
        elif f.op_tag == "2":
            c1 = cgraph.addNode("linear", inputs)
            c2 = cgraph.addNode("mod", [c1])
            c3 = cgraph.addNode("pow", [c2])
            set_params(c1, [1.0,0.0])
            set_params(c3, [2.0])
            node_map[f.expr] = c3
        elif f.op_tag == "+":
            c1 = cgraph.addNode("linear", inputs)
            set_params(c1, [1.0,1.0,0.0])
            node_map[f.expr] = c1
        elif f.op_tag == "-":
            c1 = cgraph.addNode("linear", inputs)
            set_params(c1, [1.0,-1.0,0.0])
            node_map[f.expr] = c1
        elif f.op_tag == ":":
            c1 = cgraph.addNode("linear", inputs[0:1])
            c2 = cgraph.addNode("linear", inputs[1:2])
            c3 = cgraph.addNode("div", [c1,c2])
            set_params(c1, [1.0,0.0])
            set_params(c2, [1.0,0.0])
            node_map[f.expr] = c3
        elif f.op_tag == "*":
            c1 = cgraph.addNode("linear", inputs[0:1])
            c2 = cgraph.addNode("linear", inputs[1:2])
            c3 = cgraph.addNode("mult", [c1,c2])
            set_params(c1, [1.0,0.0])
            set_params(c2, [1.0,0.0])
            node_map[f.expr] = c3
    log << log.endl
    log << "=> Created graph with %d nodes" % cgraph.size << log.endl
    return cgraph, node_map

def check_translation(fgraph, cgraph, node_map, X, verbose=False):
    log << "Checking cgraph" << log.endl
    cgraph.evaluate(X, str(X.dtype))
    for fnode in fgraph:
        cnode = node_map[fnode.expr]
        if not cnode.active: continue
        vals = fgraph.evaluateSingleNode(fnode, X, str(X.dtype))[:,0]
        cvals = cnode.vals("float64")
        diff = np.max(np.abs(vals-cvals))/(np.max(vals)-np.min(vals))
        if verbose: log << "%-40s %+1.7e" % (fnode.expr, diff) << log.endl
        if diff > 1e-5:
            log << vals << log.endl
            log << cvals << log.endl
            assert(False)
    log << "- OK" << log.endl

def filter_graph(fgraph, cgraph, node_map, mask=lambda f: f.q >= 0.99):
    fnodes_q = filter(lambda f: f.q >= 0.99, fgraph.nodes())
    cnodes_q = [ node_map[f.expr] for f in fnodes_q ]
    probs_active = np.array([ np.abs(fnode.cov*fnode.q) for fnode in fnodes_q ])
    for node in cgraph.nodes(): node.active = False
    for node in cnodes_q: node.setBranchActive(True)
    cgraph.bypassInactiveNodes()
    cnodes_active = filter(lambda f: f.active, cgraph.nodes())
    # DECLARE OUTPUT AND OBJECTIVE
    outnode = cgraph.addOutput("linear", cnodes_q)
    set_params(outnode, 
        np.array([ (1./len(cnodes_q) if f.cov > 0 else -1./len(cnodes_q)) 
            for c,f in zip(cnodes_q, fnodes_q) ] + [0.]))
    tarnode = cgraph.addTarget()
    objnode = cgraph.addObjective("mse", [ outnode, tarnode ])
    log << "Selected" << len(fnodes_q) << "/" << len(
        fgraph.nodes()) << "output nodes" << log.endl
    log << "Total # active nodes:" << len(cnodes_active) << log.endl
    log << "Activity prob's vary between %1.4f and %1.4f" % (
        np.min(probs_active), np.max(probs_active)) << log.endl
    log << "Output node of type '%s'" % outnode.op << log.endl
    return cnodes_q, probs_active

def optimize(cgraph, X, Y, 
        opt=None, 
        dropout=None,
        method="steep",
        do_resample=False,
        n_batches=1,
        n_steps_batch=100,
        rate=0.001):
    # OPTIMIZER
    if opt is None:
        options = soap.Options()
        options.set("opt.type", method)
        opt = soap.CGraphOptimizer(options)
    # FIT
    t0 = time.time()
    for i in range(n_batches):
        log << "Batch %3d  " % i << log.flush
        if do_resample:
            subsel = np.random.randint(0, X.shape[0], size=(X.shape[0],))
        else:
            subsel = np.arange(X.shape[0])
        if dropout is not None: dropout.sample()
        opt.step(cgraph, X[subsel], Y[subsel], str(X.dtype), str(Y.dtype), n_steps_batch, rate)
    t1 = time.time()
    log << "Time taken for optimization: %+1.4fs" % (t1-t0) << log.endl

def predict(cgraph, dropout, X, n_estimators):
    soap.toggle_logger()
    outputs = cgraph.outputs()
    assert len(outputs) == 1
    yp_ens = []
    t0 = time.time()
    for i in range(n_estimators):
        if dropout is not None: dropout.sample()
        cgraph.evaluate(X, str(X.dtype))
        yp = outputs[0].vals(dtype)
        yp_ens.append(yp)
    t1 = time.time()
    yp_ens = np.array(yp_ens)
    yp = np.average(yp_ens, axis=0)
    log << "Time taken for prediction: %+1.4fs" % (t1-t0) << log.endl
    return yp

