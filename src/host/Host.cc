/* ------------------------------------------------------------------------ */
/* Copyright 2002-2014, OpenNebula Project (OpenNebula.org), C12G Labs      */
/*                                                                          */
/* Licensed under the Apache License, Version 2.0 (the "License"); you may  */
/* not use this file except in compliance with the License. You may obtain  */
/* a copy of the License at                                                 */
/*                                                                          */
/* http://www.apache.org/licenses/LICENSE-2.0                               */
/*                                                                          */
/* Unless required by applicable law or agreed to in writing, software      */
/* distributed under the License is distributed on an "AS IS" BASIS,        */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. */
/* See the License for the specific language governing permissions and      */
/* limitations under the License.                                           */
/* ------------------------------------------------------------------------ */

#include <limits.h>
#include <string.h>

#include <iostream>
#include <sstream>

#include "Host.h"
#include "Nebula.h"

/* ************************************************************************ */
/* Host :: Constructor/Destructor                                           */
/* ************************************************************************ */

Host::Host(
    int id,
    const string& _hostname,
    const string& _im_mad_name,
    const string& _vmm_mad_name,
    const string& _vnm_mad_name,
    int           _cluster_id,
    const string& _cluster_name):
        PoolObjectSQL(id,HOST,_hostname,-1,-1,"","",table),
        Clusterable(_cluster_id, _cluster_name),
        state(INIT),
        im_mad_name(_im_mad_name),
        vmm_mad_name(_vmm_mad_name),
        vnm_mad_name(_vnm_mad_name),
        last_monitored(0),
        vm_collection("VMS")
{
    obj_template = new HostTemplate;
}

Host::~Host()
{
    delete obj_template;
}

/* ************************************************************************ */
/* Host :: Database Access Functions                                        */
/* ************************************************************************ */

const char * Host::table = "host_pool";

const char * Host::db_names =
    "oid, name, body, state, last_mon_time, uid, gid, owner_u, group_u, other_u, cid";

const char * Host::db_bootstrap = "CREATE TABLE IF NOT EXISTS host_pool ("
    "oid INTEGER PRIMARY KEY, name VARCHAR(128), body MEDIUMTEXT, state INTEGER, "
    "last_mon_time INTEGER, uid INTEGER, gid INTEGER, owner_u INTEGER, "
    "group_u INTEGER, other_u INTEGER, cid INTEGER, UNIQUE(name))";


const char * Host::monit_table = "host_monitoring";

const char * Host::monit_db_names = "hid, last_mon_time, body";

const char * Host::monit_db_bootstrap = "CREATE TABLE IF NOT EXISTS "
    "host_monitoring (hid INTEGER, last_mon_time INTEGER, body MEDIUMTEXT, "
    "PRIMARY KEY(hid, last_mon_time))";
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

int Host::insert_replace(SqlDB *db, bool replace, string& error_str)
{
    ostringstream   oss;

    int    rc;
    string xml_body;

    char * sql_hostname;
    char * sql_xml;

    // Set the owner and group to oneadmin
    set_user(0, "");
    set_group(GroupPool::ONEADMIN_ID, GroupPool::ONEADMIN_NAME);

   // Update the Host

    sql_hostname = db->escape_str(name.c_str());

    if ( sql_hostname == 0 )
    {
        goto error_hostname;
    }

    sql_xml = db->escape_str(to_xml(xml_body).c_str());

    if ( sql_xml == 0 )
    {
        goto error_body;
    }

    if ( validate_xml(sql_xml) != 0 )
    {
        goto error_xml;
    }

    if(replace)
    {
        oss << "REPLACE";
    }
    else
    {
        oss << "INSERT";
    }

    // Construct the SQL statement to Insert or Replace

    oss <<" INTO "<<table <<" ("<< db_names <<") VALUES ("
        <<          oid                 << ","
        << "'" <<   sql_hostname        << "',"
        << "'" <<   sql_xml             << "',"
        <<          state               << ","
        <<          last_monitored      << ","
        <<          uid                 << ","
        <<          gid                 << ","
        <<          owner_u             << ","
        <<          group_u             << ","
        <<          other_u             << ","
        <<          cluster_id          << ")";

    rc = db->exec(oss);

    db->free_str(sql_hostname);
    db->free_str(sql_xml);

    return rc;

error_xml:
    db->free_str(sql_hostname);
    db->free_str(sql_xml);

    error_str = "Error transforming the Host to XML.";

    goto error_common;

error_body:
    db->free_str(sql_hostname);
    goto error_generic;

error_hostname:
    goto error_generic;

error_generic:
    error_str = "Error inserting Host in DB.";
error_common:
    return -1;
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

int Host::extract_ds_info(
            string          &parse_str,
            Template        &tmpl,
            map<int, const VectorAttribute*> &ds)
{
    char *    error_msg;
    int       rc;

    const VectorAttribute *  vatt;
    vector<const Attribute*> ds_att;

    vector<const Attribute*>::const_iterator it;

    // -------------------------------------------------------------------------
    // Parse Template
    // -------------------------------------------------------------------------
    rc = tmpl.parse(parse_str, &error_msg);

    if ( rc != 0 )
    {
        ostringstream ess;

        ess << "Error parsing host information: " << error_msg
            << ". Monitoring information: " << endl << parse_str;

        NebulaLog::log("ONE", Log::ERROR, ess);

        touch(false);

        set_template_error_message("Error parsing monitor information."
            " Check oned.log for more details.");

        free(error_msg);

        return -1;
    }

    // -------------------------------------------------------------------------
    // Get DS information
    // -------------------------------------------------------------------------
    tmpl.get("DS", ds_att);

    for (it = ds_att.begin(); it != ds_att.end(); it++)
    {
        int dsid;

        vatt = dynamic_cast<const VectorAttribute*>(*it);

        if (vatt == 0)
        {
            continue;
        }

        rc = vatt->vector_value("ID", dsid);

        if (rc == 0 && dsid != -1)
        {
            ds.insert(make_pair(dsid, vatt));
        }
    }

    return 0;
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

int Host::update_info(Template        &tmpl,
                      bool            &with_vm_info,
                      set<int>        &lost,
                      map<int,string> &found,
                      const set<int>  &non_shared_ds)
{
    VectorAttribute*             vatt;
    vector<Attribute*>::iterator it;
    vector<Attribute*>           vm_att;
    vector<Attribute*>           ds_att;
    vector<Attribute*>           local_ds_att;

    int   rc;
    int   vmid;
    float val;

    string error_st;

    ostringstream zombie;
    ostringstream wild;

    set<int>::iterator set_it;

    set<int> prev_tmp_lost   = tmp_lost_vms;
    set<int> prev_tmp_zombie = tmp_zombie_vms;

    int num_zombies = 0;
    int num_wilds   = 0;

    // -------------------------------------------------------------------------
    // Remove expired information from current template
    // -------------------------------------------------------------------------
    clear_template_error_message();

    remove_template_attribute("ZOMBIES");
    remove_template_attribute("TOTAL_ZOMBIES");

    remove_template_attribute("WILDS");
    remove_template_attribute("TOTAL_WILDS");

    remove_template_attribute("VM");
    remove_template_attribute("VM_POLL");

    remove_template_attribute("DS");

    // -------------------------------------------------------------------------
    // Copy monitor, extract share info & update last_monitored and state
    // -------------------------------------------------------------------------

    obj_template->merge(&tmpl, error_st);

    touch(true);

    if (isEnabled())
    {
        erase_template_attribute("TOTALCPU", val);
        host_share.max_cpu = val;
        erase_template_attribute("TOTALMEMORY", val);
        host_share.max_mem = val;
        erase_template_attribute("DS_LOCATION_TOTAL_MB", val);
        host_share.max_disk = val;

        erase_template_attribute("FREECPU", val);
        host_share.free_cpu = val;
        erase_template_attribute("FREEMEMORY", val);
        host_share.free_mem = val;
        erase_template_attribute("DS_LOCATION_FREE_MB", val);
        host_share.free_disk = val;

        erase_template_attribute("USEDCPU", val);
        host_share.used_cpu = val;
        erase_template_attribute("USEDMEMORY", val);
        host_share.used_mem = val;
        erase_template_attribute("DS_LOCATION_USED_MB", val);
        host_share.used_disk = val;
    }

    // -------------------------------------------------------------------------
    // Correlate VM information with the list of running VMs
    // -------------------------------------------------------------------------

    erase_template_attribute("VM_POLL", with_vm_info);

    obj_template->remove("VM", vm_att);

    tmp_lost_vms = vm_collection.get_collection_copy();

    tmp_zombie_vms.clear();

    for (it = vm_att.begin(); it != vm_att.end(); it++)
    {
        vatt = dynamic_cast<VectorAttribute*>(*it);

        if (vatt == 0)
        {
            delete *it;
            continue;
        }

        rc = vatt->vector_value("ID", vmid);

        if (rc == 0 && vmid != -1)
        {
            if (tmp_lost_vms.erase(vmid) == 1) //Good, known
            {
                found.insert(make_pair(vmid, vatt->vector_value("POLL")));
            }
            else //Bad, known but should not be here
            {
                tmp_zombie_vms.insert(vmid);

                // Reported as zombie at least 2 times?
                if (prev_tmp_zombie.count(vmid) == 1)
                {
                    if (num_zombies++ > 0)
                    {
                        zombie << ", ";
                    }

                    zombie << vatt->vector_value("DEPLOY_ID");
                }
            }
        }
        else if (rc == 0) //not ours
        {
            if (num_wilds++ > 0)
            {
                wild << ", ";
            }

            wild << vatt->vector_value("DEPLOY_ID");
        }

        delete *it;
    }

    for(set_it = tmp_lost_vms.begin(); set_it != tmp_lost_vms.end(); set_it++)
    {
        // Reported as lost at least 2 times?
        if (prev_tmp_lost.count(*set_it) == 1)
        {
            lost.insert(*set_it);
        }
    }

    if (num_wilds > 0)
    {
        add_template_attribute("TOTAL_WILDS", num_wilds);
        add_template_attribute("WILDS", wild.str());
    }

    if (num_zombies > 0)
    {
        add_template_attribute("TOTAL_ZOMBIES", num_zombies);
        add_template_attribute("ZOMBIES", zombie.str());
    }

    // -------------------------------------------------------------------------
    // Copy system datastore monitorization (non_shared) to host share
    // -------------------------------------------------------------------------

    obj_template->remove("DS", ds_att);

    for (it = ds_att.begin(); it != ds_att.end(); it++)
    {
        int dsid;

        vatt = dynamic_cast<VectorAttribute*>(*it);

        if (vatt == 0)
        {
            delete *it;
            continue;
        }

        rc = vatt->vector_value("ID", dsid);

        if (rc == 0 && non_shared_ds.count(dsid) == 1)
        {
            local_ds_att.push_back(vatt);
        }
        else
        {
            delete *it;
        }
    }

    host_share.set_ds_monitorization(local_ds_att);

    return 0;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void Host::disable()
{
    state = DISABLED;

    host_share.max_cpu = 0;
    host_share.max_mem = 0;

    host_share.free_cpu = 0;
    host_share.free_mem = 0;

    host_share.used_cpu = 0;
    host_share.used_mem = 0;

    remove_template_attribute("TOTALCPU");
    remove_template_attribute("TOTALMEMORY");

    remove_template_attribute("FREECPU");
    remove_template_attribute("FREEMEMORY");

    remove_template_attribute("USEDCPU");
    remove_template_attribute("USEDMEMORY");
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void Host::error_info(const string& message, set<int> &vm_ids)
{
    ostringstream oss;

    vm_ids = vm_collection.get_collection_copy();

    oss << "Error monitoring Host " << get_name() << " (" << get_oid() << ")"
        << ": " << message;

    NebulaLog::log("ONE", Log::ERROR, oss);

    touch(false);

    set_template_error_message(oss.str());
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

int Host::update_monitoring(SqlDB * db)
{
    ostringstream   oss;
    int             rc;

    string xml_body;
    string error_str;
    char * sql_xml;

    sql_xml = db->escape_str(to_xml(xml_body).c_str());

    if ( sql_xml == 0 )
    {
        goto error_body;
    }

    if ( validate_xml(sql_xml) != 0 )
    {
        goto error_xml;
    }

    oss << "REPLACE INTO " << monit_table << " ("<< monit_db_names <<") VALUES ("
        <<          oid             << ","
        <<          last_monitored       << ","
        << "'" <<   sql_xml         << "')";

    db->free_str(sql_xml);

    rc = db->exec(oss);

    return rc;

error_xml:
    db->free_str(sql_xml);

    error_str = "could not transform the Host to XML.";

    goto error_common;

error_body:
    error_str = "could not insert the Host in the DB.";

error_common:
    oss.str("");
    oss << "Error updating Host monitoring information, " << error_str;

    NebulaLog::log("ONE",Log::ERROR, oss);

    return -1;
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

bool Host::is_public_cloud() const
{
    bool is_public_cloud = false;

    get_template_attribute("PUBLIC_CLOUD", is_public_cloud);

    return is_public_cloud;
}

/* ************************************************************************ */
/* Host :: Misc                                                             */
/* ************************************************************************ */

string& Host::to_xml(string& xml) const
{
    string template_xml;
    string share_xml;

    ostringstream oss;
    string        vm_collection_xml;

    oss <<
    "<HOST>"
       "<ID>"            << oid       	   << "</ID>"            <<
       "<NAME>"          << name 	       << "</NAME>"          <<
       "<STATE>"         << state          << "</STATE>"         <<
       "<IM_MAD>"        << im_mad_name    << "</IM_MAD>"        <<
       "<VM_MAD>"        << vmm_mad_name   << "</VM_MAD>"        <<
       "<VN_MAD>"        << vnm_mad_name   << "</VN_MAD>"        <<
       "<LAST_MON_TIME>" << last_monitored << "</LAST_MON_TIME>" <<
       "<CLUSTER_ID>"    << cluster_id     << "</CLUSTER_ID>"    <<
       "<CLUSTER>"       << cluster        << "</CLUSTER>"       <<
       host_share.to_xml(share_xml)  <<
       vm_collection.to_xml(vm_collection_xml) <<
       obj_template->to_xml(template_xml) <<
    "</HOST>";

    xml = oss.str();

    return xml;
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

int Host::from_xml(const string& xml)
{
    vector<xmlNodePtr> content;

    int int_state;
    int rc = 0;

    // Initialize the internal XML object
    update_from_str(xml);

    // Get class base attributes
    rc += xpath(oid, "/HOST/ID", -1);
    rc += xpath(name, "/HOST/NAME", "not_found");
    rc += xpath(int_state, "/HOST/STATE", 0);

    rc += xpath(im_mad_name, "/HOST/IM_MAD", "not_found");
    rc += xpath(vmm_mad_name, "/HOST/VM_MAD", "not_found");
    rc += xpath(vnm_mad_name, "/HOST/VN_MAD", "not_found");

    rc += xpath(last_monitored, "/HOST/LAST_MON_TIME", 0);

    rc += xpath(cluster_id, "/HOST/CLUSTER_ID", -1);
    rc += xpath(cluster,    "/HOST/CLUSTER",    "not_found");

    state = static_cast<HostState>( int_state );

    // Set the owner and group to oneadmin
    set_user(0, "");
    set_group(GroupPool::ONEADMIN_ID, GroupPool::ONEADMIN_NAME);

    // ------------ Host Share ---------------

    ObjectXML::get_nodes("/HOST/HOST_SHARE", content);

    if (content.empty())
    {
        return -1;
    }

    rc += host_share.from_xml_node( content[0] );

    ObjectXML::free_nodes(content);

    content.clear();

    // ------------ Host Template ---------------

    ObjectXML::get_nodes("/HOST/TEMPLATE", content);

    if( content.empty())
    {
        return -1;
    }

    rc += obj_template->from_xml_node( content[0] );

    ObjectXML::free_nodes(content);

    content.clear();

    // ------------ VMS collection ---------------

    ObjectXML::get_nodes("/HOST/VMS", content);

    if (content.empty())
    {
        return -1;
    }

    rc += vm_collection.from_xml_node(content[0]);

    ObjectXML::free_nodes(content);

    if (rc != 0)
    {
        return -1;
    }

    return 0;
}
