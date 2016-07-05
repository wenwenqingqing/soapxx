import os
import numpy as np
import multiprocessing as mp
import functools as fct
import json
import datetime
import resource

def mp_compute_column_block(gi, gj_list, kfct):
    """
    Evaluates kfct for each pair (gi, gj), with gj from gj_list

    See Also
    --------
    mp_compute_upper_triangle
    """
    krow = []
    for gj in gj_list:
        if gj.mp_idx < gi.mp_idx:
            k = 0.
        else:
            k = kfct(gi, gj)
        krow.append(k)
    return krow

def mp_compute_upper_triangle(kfct, g_list, n_procs, n_blocks, log=None, tstart_twall=(None,None), **kwargs):
    """
    Compute kernel matrix computed from pairs of objects in object list

    Parameters
    ----------
    kfct : function reference to be evaluated between g_list items
    g_list : list of items supplied to kfct(gi, gj, **kwargs)
    n_procs: number of processors requested
    n_blocks: number of column blocks onto which computation is split
    kwargs: keyword arguments supplied to kfct
    """
    t_start = tstart_twall[0]
    t_wall = tstart_twall[1]
    dim = len(g_list)
    kmat = np.zeros((dim,dim))
    # Embed mp index in g-list objects
    for mp_idx, g in enumerate(g_list): g.mp_idx = mp_idx
    # Divide onto column blocks
    col_idcs = np.arange(len(g_list))
    col_div_list = np.array_split(col_idcs, n_blocks)
    for col_div_idx, col_div in enumerate(col_div_list):
        t_in = datetime.datetime.now()
        # Column start, column end
        c0 = col_div[0]
        c1 = col_div[-1]+1
        if log: log << "Column block i[%d:%d] j[%d:%d]" % (0, c1, c0, c1) << log.endl
        gj_list = g_list[c0:c1]
        gi_list = g_list[0:c1]
        # Prime kernel function
        kfct_primed = fct.partial(
            kfct,
            **kwargs)
        pool = mp.Pool(processes=n_procs)
        # Prime mp function
        mp_compute_column_block_primed = fct.partial(
            mp_compute_column_block,
            gj_list=gj_list,
            kfct=kfct_primed)
        # Map & close
        npyfile = 'out.block_i_%d_%d_j_%d_%d.npy' % (0, c1, c0, c1)
        # ... but first check for previous calculations of same slice
        if npyfile in os.listdir('./'):
            if log: log << "Load block from '%s'" % npyfile << log.endl
            kmat_column_block = np.load(npyfile)
        else:
            kmat_column_block = pool.map(mp_compute_column_block_primed, gi_list)
            kmat_column_block = np.array(kmat_column_block)
            np.save(npyfile, kmat_column_block)
        # Update kernel matrix
        kmat[0:c1,c0:c1] = kmat_column_block
        pool.close()
        pool.join()
        # Check time
        t_out = datetime.datetime.now()
        dt_block = t_out-t_in
        if t_start and t_wall:
            t_elapsed = t_out-t_start
            if log: log << "Time elapsed =" << t_elapsed << " (wall time = %s) (maxmem = %d)" % (t_wall, resource.getrusage(resource.RUSAGE_SELF).ru_maxrss) << log.endl
            if col_div_idx+1 != len(col_div_list) and t_elapsed+dt_block > t_wall-dt_block:
                log << "Wall time hit expected for next iteration, break ..." << log.endl
                break
            else: pass
    return kmat

def json_load_utf8(file_handle):
    return _byteify(
        json.load(file_handle, object_hook=_byteify),
        ignore_dicts=True
    )

def json_loads_utf8(json_text):
    return _byteify(
        json.loads(json_text, object_hook=_byteify),
        ignore_dicts=True
    )

def _byteify(data, ignore_dicts = False):
    # if this is a unicode string, return its string representation
    if isinstance(data, unicode):
        return data.encode('utf-8')
    # if this is a list of values, return list of byteified values
    if isinstance(data, list):
        return [ _byteify(item, ignore_dicts=True) for item in data ]
    # if this is a dictionary, return dictionary of byteified keys and values
    # but only if we haven't already byteified it
    if isinstance(data, dict) and not ignore_dicts:
        return {
            _byteify(key, ignore_dicts=True): _byteify(value, ignore_dicts=True)
            for key, value in data.iteritems()
        }
    # if it's anything else, return it in its original form
    return data
