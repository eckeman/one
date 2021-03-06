/* -------------------------------------------------------------------------- */
/* Copyright 2002-2014, OpenNebula Project (OpenNebula.org), C12G Labs        */
/*                                                                            */
/* Licensed under the Apache License, Version 2.0 (the "License"); you may    */
/* not use this file except in compliance with the License. You may obtain    */
/* a copy of the License at                                                   */
/*                                                                            */
/* http://www.apache.org/licenses/LICENSE-2.0                                 */
/*                                                                            */
/* Unless required by applicable law or agreed to in writing, software        */
/* distributed under the License is distributed on an "AS IS" BASIS,          */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   */
/* See the License for the specific language governing permissions and        */
/* limitations under the License.                                             */
/* -------------------------------------------------------------------------- */

#include "Quotas.h"
#include "Nebula.h"

#include "ObjectXML.h"

int Quotas::set(Template *tmpl, string& error)
{
    vector<Attribute *> vquotas;

    if ( tmpl->get(datastore_quota.get_quota_name(), vquotas) > 0 )
    {
        if ( datastore_quota.set(&vquotas, error) != 0 )
        {
            return -1;
        }

        vquotas.clear();
    }

    if ( tmpl->get(network_quota.get_quota_name(), vquotas) > 0 )
    {
        if ( network_quota.set(&vquotas, error) != 0 )
        {
            return -1;
        }

        vquotas.clear();
    }

    if ( tmpl->get(image_quota.get_quota_name(), vquotas) > 0 )
    {
        if ( image_quota.set(&vquotas, error) != 0 )
        {
            return -1;
        }

        vquotas.clear();
    }

    if ( tmpl->get(vm_quota.get_quota_name(), vquotas) > 0 )
    {
        if ( vm_quota.set(&vquotas, error) != 0 )
        {
            return -1;
        }

        vquotas.clear();
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

string& Quotas::to_xml(string& xml) const
{
    ostringstream oss;

    string ds_quota_xml;
    string net_quota_xml;
    string vm_quota_xml;
    string image_quota_xml;

    oss << datastore_quota.to_xml(ds_quota_xml)
        << network_quota.to_xml(net_quota_xml)
        << vm_quota.to_xml(vm_quota_xml)
        << image_quota.to_xml(image_quota_xml);

    xml = oss.str();

    return xml;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

int Quotas::from_xml(ObjectXML * object_xml)
{
    vector<xmlNodePtr> content;
    int                rc = 0;

    object_xml->get_nodes(ds_xpath, content);

    if (!content.empty())
    {
        rc += datastore_quota.from_xml_node(content[0]);
    }

    object_xml->free_nodes(content);
    content.clear();

    object_xml->get_nodes(net_xpath, content);

    if (!content.empty())
    {
        rc += network_quota.from_xml_node(content[0]);
    }

    object_xml->free_nodes(content);
    content.clear();

    object_xml->get_nodes(vm_xpath, content);

    if (!content.empty())
    {
        rc += vm_quota.from_xml_node(content[0]);
    }

    object_xml->free_nodes(content);
    content.clear();

    object_xml->get_nodes(img_xpath, content);

    if (!content.empty())
    {
        rc += image_quota.from_xml_node(content[0]);
    }

    object_xml->free_nodes(content);

    return rc;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void Quotas::quota_del(QuotaType type, Template *tmpl)
{
    switch (type)
    {
        case DATASTORE:
            datastore_quota.del(tmpl);
        break;

        case NETWORK:
            network_quota.del(tmpl);
        break;

        case IMAGE:
            image_quota.del(tmpl);
        break;

        case VM:
            vm_quota.del(tmpl);
        break;

        case VIRTUALMACHINE:
            network_quota.del(tmpl);
            vm_quota.del(tmpl);
            image_quota.del(tmpl);
        break;
    }
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

bool Quotas::quota_check(QuotaType  type,
                        Template    *tmpl,
                        Quotas      &default_quotas,
                        string      &error_str)
{
    switch (type)
    {
        case DATASTORE:
            return datastore_quota.check(tmpl, default_quotas, error_str);

        case NETWORK:
            return network_quota.check(tmpl, default_quotas, error_str);

        case IMAGE:
            return image_quota.check(tmpl, default_quotas, error_str);

        case VM:
            return vm_quota.check(tmpl, default_quotas, error_str);

        case VIRTUALMACHINE:
            if ( network_quota.check(tmpl, default_quotas, error_str) == false )
            {
                return false;
            }

            if ( vm_quota.check(tmpl, default_quotas, error_str) == false )
            {
                network_quota.del(tmpl);
                return false;
            }

            if ( image_quota.check(tmpl, default_quotas, error_str) == false )
            {
                network_quota.del(tmpl);
                vm_quota.del(tmpl);
                return false;
            }

            return true;
    }

    return false;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

bool Quotas::quota_update(QuotaType  type,
                          Template    *tmpl,
                          Quotas      &default_quotas,
                          string      &error_str)
{
    switch (type)
    {
        // This is an internal check, should never get in here.
        case DATASTORE:
        case NETWORK:
        case IMAGE:
        case VIRTUALMACHINE:
            error_str = "Cannot update quota. Not implemented";
            return false;

        case VM:
            return vm_quota.update(tmpl, default_quotas, error_str);
    }

    return false;
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void Quotas::quota_del(QuotaType type, int uid, int gid, Template * tmpl)
{
    Nebula&     nd    = Nebula::instance();
    UserPool *  upool = nd.get_upool();
    GroupPool * gpool = nd.get_gpool();

    User *  user;
    Group * group;

    if ( uid != UserPool::ONEADMIN_ID )
    {
        user = upool->get(uid, true);

        if ( user != 0 )
        {
            user->quota.quota_del(type, tmpl);

            upool->update_quotas(user);

            user->unlock();
        }
    }

    if ( gid != GroupPool::ONEADMIN_ID )
    {
        group = gpool->get(gid, true);

        if ( group != 0 )
        {
            group->quota.quota_del(type, tmpl);

            gpool->update_quotas(group);

            group->unlock();
        }
    }
}
