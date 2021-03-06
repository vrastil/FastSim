"""
'database.py' module serves for creating, updating and managinh databse with
all data from simulations
"""

# system packages
from __future__ import print_function
import os
import sys
import pymongo
import pickle
from bson.binary import Binary
from bson.son import SON
from getpass import getpass
import json
import datetime
import numpy

# simpy packages
from .struct import RESULTS_DIRS
from . import utils as ut

# version independent raw_input
if sys.version[0]=="3": raw_input=input

def create_database(host='localhost', port=27017, user='admin'):
    """create database and user admin in it,
    databse should be started without authentication this first time"""

    print("Database server should be running without authentication on host '%s' and port '%i'" % (
        host, port))
    raw_input("Press Enter to continue...")
    client = pymongo.MongoClient(host, port)
    db = client['admin']
    try:
        user = raw_input("Username (default '%s'):" % user) or user
        db.command("createUser", user, pwd=getpass(prompt='Password:'), roles=[{'role':'userAdminAnyDatabase','db':'admin'}, "readWriteAnyDatabase"])
    except pymongo.errors.DuplicateKeyError as e:
        print(e)
    else:
        print("User '%s' successfully created. Restart the server with authentication enabled." % user)

def connect_db(host='localhost', port=27017, user='admin'):
    for _ in range(3):
        try:
            client = pymongo.MongoClient(host, port, username=user, password=getpass(prompt='Password:'))
            client['admin'].list_collection_names()
        except pymongo.errors.OperationFailure as e:
            print(e)
        else:
            print("Successfully conected to the database.")
            return client

def create_indices(db, collection='data'):
    db[collection].create_index([
        ("app", 1),
        ("box_opt.mesh_num", 1),
        ("box_opt.par_num", 1),
        ("box_opt.mesh_num_pwr", 1),
        ("box_opt.box_size", 1)
    ])

def get_id_keys(db, app, collection='data'):
    doc = db[collection].find_one({'app' : app}, {
        '_id' : 0,
        'app' : 0,
        'app_opt' : 0,
        'data' : 0,
        'database' : 0,
        'out_opt' : 0,
        'results' : 0,
        'run_opt' : 0,
        'type' : 0
    })

    all_keys = {}
    for key in doc.keys():
        all_keys[key] = doc[key].keys()

    if app != 'TZA':
        all_keys["cosmo"].remove("smoothing_k")

    # some (new) runs do not have cosmo['A'], it is always 1
    try:
        all_keys["cosmo"].remove("A")
    except ValueError:
        pass # key not present
    
    return all_keys

sep_str = '::'

def get_pipeline(db, app, info_type='sim_info', query=None):
    _query = {'app' : app, 'type' : info_type}
    if query is not None:
        if 'app' in query and query['app'] != app:
            return None

        if 'type' in query and query['type'] != info_type:
            return None

    keys = get_id_keys(db, app)
    group = {"_id" : {}}
    for opt_key, opt_key_list in keys.items():
        for key in opt_key_list:
            group["_id"]["%s%s%s" % (opt_key, sep_str, key)] = "$%s.%s" % (opt_key, key)

    group["count"] = {"$sum": 1}
    pipeline = [
        {"$match" : _query },
        {"$group" : group },
        {"$sort": SON([
            ("_id.box_opt%smesh_num" % sep_str, 1),
            ("_id.box_opt%smesh_num_pwr" % sep_str, 1),
            ("_id.box_opt%spar_num" % sep_str, 1),
            ("_id.box_opt%sbox_size" % sep_str, 1),
            ("_id.integ_opt%stime_step" % sep_str, 1)
            ])}
    ]

    if app == 'CHI':
        pipeline[2]["$sort"]["_id.chi_opt%sphi" % sep_str] = 1
        pipeline[2]["$sort"]["_id.chi_opt%sn" % sep_str] = 1
    elif app == 'TZA':
        pipeline[2]["$sort"]["_id.cosmo%ssmoothing_k" % sep_str] = 1

    return pipeline

def print_db_info(db, collection='data', info_type='sim_info'):
    apps = db[collection].distinct('app', {})
    print("There are in total %i number of simulations." % db[collection].count_documents({'type' : info_type}))
    for app in apps:
        print("%s (%i):" % (app, db[collection].count_documents({'app' : app})))

        pipeline = get_pipeline(db, app)
        for doc in db[collection].aggregate(pipeline):
            _id = doc["_id"]
            msg = "\tNm = %i, NM = %i, Np = %i, L = %i, Nt = %i" % (
                _id["box_opt%smesh_num" % sep_str],
                _id["box_opt%smesh_num_pwr" % sep_str],
                _id["box_opt%spar_num" % sep_str],
                _id["box_opt%sbox_size" % sep_str],
              1/_id[u"integ_opt%stime_step" % sep_str],
                )
            if app == 'CHI' or app == 'CHI_FF':
                msg += ", phi = %.1E, n = %.1f" % (
                    _id["chi_opt%sphi" % sep_str],
                    _id["chi_opt%sn" % sep_str]
                )
                if _id['chi_opt%slinear' % sep_str]:
                    msg += ' (linear)'
                else:
                    msg += ' (non-linear)'
            elif app == 'TZA':
                msg += ", k_G = %.1f" %  numpy.sqrt(_id["cosmo%ssmoothing_k" % sep_str])
            msg += ", num = %i" % doc['count']

            query = {'app' : app, 'type' : info_type, 'data.files.vel_pwr_spec' : {"$exists": True }}
            for key, val in _id.items():
                new_key = key.replace(sep_str, '.')
                query[new_key] = val

            count_vel = db.data.count_documents(query)
            if count_vel:
                msg += ", num_vel = %i" % count_vel
            print(msg)

def get_separated_ids(db, collection='data', info_type='sim_info', query=None):
    apps = db[collection].distinct('app', {})
    sep_id = []
    i = 0
    # go by application
    for app in apps:
        pipeline = get_pipeline(db, app, info_type=info_type, query=query)
        if pipeline is None:
            continue
        # separate by different runs
        for doc in db[collection].aggregate(pipeline):
            sep_id.append([])
            # get document by which we can find all belonging docs
            new_doc = {'app' : app, 'type' : info_type}
            for key, val in doc['_id'].items():
                new_key = key.replace(sep_str, '.')
                new_doc[new_key] = val
            # get only id of these runs
            cursor = db[collection].find(new_doc, {'_id' : 1})
            for x in cursor:
                sep_id[i].append(x)

            # increase counter
            i += 1
    return sep_id

def is_new_sim(sim_param, override):
    # if "database" is not in sim_param.json, we did not processed this run
    if "database" in sim_param:
        # in case we do not want to load data again
        if not override:
            return False
        # in case we do want to load ALL data
        elif isinstance(override, bool):
            return True
        # in case we do want to load only old data
        elif isinstance(override, datetime.datetime):
            return datetime.datetime.strptime(sim_param["database"]["timestamp"], '%Y-%m-%d %H:%M:%S.%f') < override      
    return True

def add_one_sim_data(a_file, db, collection='data', override=False, collection_large='data_large'):
    """load simulation parameters with available data and save them into database,
    return true if the record was not in the database before"""

    # open sim_param.json
    with open(a_file) as json_file:
        data = ut.get_sorted_bson(json.loads(json_file.read()))

    # check if we already loaded this simulation
    if not is_new_sim(data, override):
        return False

    # get rid of unwanted data
    data.pop("results", None)
    data.pop("database", None)
    data["out_opt"].pop("out_dir")
    data["run_opt"].pop("num_thread")

    # extract app
    app = data['app']
    
    # directory with results
    dirs = set(RESULTS_DIRS.values())
    root_dir = os.path.dirname(a_file) + '/'

    # add subdocs
    data["type"] = 'sim_info'
    data['data'] = {
        'files' : {},
        'processed' : {}
    }
    data["out_opt"]["dir"] = root_dir
    data["out_opt"]["file"] = a_file
    data_files_dict = data['data']['files']

    # load all the files and add data to the database
    # go through all possible directories
    for data_dir in dirs:
        data_files_dict[data_dir] = []
        # go through all files in the directory
        for data_file in ut.get_files_in_traverse_dir(root_dir + data_dir, '*'):
            # load data and save them in binary
            binary_data = pickle.dumps(numpy.transpose(numpy.loadtxt(data_file)))
            data_files_dict[data_dir].append({
                'file' : data_file,
                'z' : ut.get_z_from_file(data_file, app),
                'data' : Binary(binary_data)
            })

        # delete empty directories from dictionary
        if not data_files_dict[data_dir]:
            del data_files_dict[data_dir]

    # large files for density maps save separately (may exceed 16MB limit for BSON)
    large_key = 'rho_map'
    if large_key in data_files_dict:
        # copy doc except the data
        data_large = {key : value for key, value in data.items() if key != 'data'}
        # remove large data from original document
        data_large['data'] = {
            'files' : {
                large_key : data_files_dict.pop(large_key)
            },
            'processed' : {}
        }

        # leave only one file
        data_large_files_list = data_large['data']['files'][large_key]
        low_z_data = min(data_large_files_list, key=lambda x: x['z'])
        
        # for rho_map we need only density, [x, z, rho]
        rho = pickle.loads(low_z_data['data'])
        rho = rho[2,:]
        low_z_data['data'] = pickle.dumps(rho)

        # save it into the database
        data_large['data']['files'][large_key] = [low_z_data]
        db[collection_large].find_one_and_update(data_large,  {"$set" : data_large}, upsert=True)

    # save eveyrthing into the database
    if db[collection].find_one_and_update(data,  {"$set" : data}, upsert=True) is None:
        new = True
    else:
        new = False

    # update sim_param.json
    with open(a_file) as json_file:
        data_orig = json.loads(json_file.read())
        data_orig.pop("results", None)
        data_orig.pop("database", None)
        data_orig["database"] = {
            "timestamp" : str(datetime.datetime.utcnow()),
            "_id" : str(db[collection].find_one(data, {'_id' : 1})['_id'])
        }
    with open(a_file, 'w') as json_file:
        json.dump(data_orig, json_file, indent=2)
    
    return new


def add_many_sim_data(a_dir, db, collection='data', a_file='sim_param.json', verbose=True, override=False):
    """add all simulations in subdirectories of 'a_dir'"""

    if verbose:
        print("Starting to populate the database with simulations...")

    # find all simulations parameters and load data
    new_docs = 0
    sim_files = ut.get_files_in_traverse_dir_stop(a_dir, a_file, stop=True, verbose=verbose)
    length = len(sim_files)
    for i, sim_file in enumerate(sim_files):
        print("Adding simulation %i from %i\r" % (i + 1, length), end='')
        new_docs += add_one_sim_data(sim_file, db, collection=collection, override=override)

    # print some useful info
    if verbose:
        print("There were added %i new simulations to the database from %i in the directory."
             % (new_docs, length))
        print_db_info(db, collection=collection)

def init_database(a_dir, host='localhost', port=27017, user='admin', init=True, override=False):
    """initialize database, create user with admin rights, add data"""
    if init:
        # start database
        create_database(host=host, port=port, user=user)

        # wait for restart of the database
        raw_input("Press Enter to continue...")

    # connect to database and fill it with data
    db = connect_db(host=host, port=port, user=user)["fastsim"]
    create_indices(db)
    add_many_sim_data(a_dir, db, override=override)

    return db
    
def add_smoothing_k_to_query(query, smoothing_k=0.25):
    query['$or'] = [
             {'cosmo.smoothing_k' : smoothing_k},
             {'cosmo.smoothing_k' : 0}
        ]