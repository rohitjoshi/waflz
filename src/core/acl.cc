//: ----------------------------------------------------------------------------
//: Copyright (C) 2016 Verizon.  All Rights Reserved.
//: All Rights Reserved
//:
//: \file:    acl.cc
//: \details: TODO
//: \author:  Devender Singh
//: \date:    07/14/2017
//:
//:   Licensed under the Apache License, Version 2.0 (the "License");
//:   you may not use this file except in compliance with the License.
//:   You may obtain a copy of the License at
//:
//:       http://www.apache.org/licenses/LICENSE-2.0
//:
//:   Unless required by applicable law or agreed to in writing, software
//:   distributed under the License is distributed on an "AS IS" BASIS,
//:   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//:   See the License for the specific language governing permissions and
//:   limitations under the License.
//:
//: ----------------------------------------------------------------------------
//: ----------------------------------------------------------------------------
//: includes
//: ----------------------------------------------------------------------------
#include "support/ndebug.h"
#include "op/regex.h"
#include "support/string_util.h"
#include "op/nms.h"
#include "jspb/jspb.h"
#include "waflz/def.h"
#include "waflz/rqst_ctx.h"
#include "waflz/acl.h"
#include "event.pb.h"
#include "acl.pb.h"
#include <errno.h>
#include <limits.h>
//: ----------------------------------------------------------------------------
//: constants
//: ----------------------------------------------------------------------------
#define _CONFIG_ACL_MAX_SIZE (1<<20)
//: ----------------------------------------------------------------------------
//: macros
//: ----------------------------------------------------------------------------
#define _GET_HEADER(_header, _val) do { \
        _val = NULL; \
        _val##_len = 0; \
        l_d.m_data = _header; \
        l_d.m_len = sizeof(_header) - 1; \
        data_map_t::const_iterator i_h = l_hm.find(l_d); \
        if(i_h != l_hm.end()) \
        { \
                _val = i_h->second.m_data; \
                _val##_len = i_h->second.m_len; \
        } \
} while(0)
namespace ns_waflz {
//: ----------------------------------------------------------------------------
//: support for skipping Content Types with idempotent methods
//: ----------------------------------------------------------------------------
struct case_i_comp
{
        bool operator() (const std::string& lhs, const std::string& rhs) const
        {
                return strcasecmp(lhs.c_str(), rhs.c_str()) < 0;
        }
};
typedef std::set <std::string, case_i_comp> str_set_t;
// ---------------------------------------------------------
// ignore method set
// ---------------------------------------------------------
const str_set_t::value_type g_ignore_ct_set_vals[]= {
        str_set_t::value_type("GET"),
        str_set_t::value_type("HEAD"),
        str_set_t::value_type("OPTIONS"),
        str_set_t::value_type("PROPFIND")
};
const str_set_t g_ignore_ct_set(g_ignore_ct_set_vals,
                                g_ignore_ct_set_vals + (sizeof(g_ignore_ct_set_vals)/sizeof(g_ignore_ct_set_vals[0])));
//: ----------------------------------------------------------------------------
//: \details ctor
//: \return  None
//: \param   None
//: ----------------------------------------------------------------------------
acl::acl(void):
        m_init(false),
        m_err_msg(),
        m_pb(NULL),
        m_id(),
        m_cust_id(),
        m_name(),
        m_ip_whitelist(NULL),
        m_ip_accesslist(NULL),
        m_ip_blacklist(NULL),
        m_country_whitelist(),
        m_country_accesslist(),
        m_country_blacklist(),
        m_asn_whitelist(),
        m_asn_accesslist(),
        m_asn_blacklist(),
        m_url_rx_whitelist(NULL),
        m_url_rx_accesslist(NULL),
        m_url_rx_blacklist(NULL),
        m_ua_rx_whitelist(NULL),
        m_ua_rx_accesslist(NULL),
        m_ua_rx_blacklist(NULL),
        m_referer_rx_whitelist(NULL),
        m_referer_rx_accesslist(NULL),
        m_referer_rx_blacklist(NULL),
        m_cookie_rx_whitelist(NULL),
        m_cookie_rx_accesslist(NULL),
        m_cookie_rx_blacklist(NULL),
        m_allowed_http_methods(),
        m_allowed_http_versions(),
        m_allowed_request_content_types(),
        m_disallowed_extensions(),
        m_disallowed_headers()
{
        m_pb = new waflz_pb::acl();
}
//: ----------------------------------------------------------------------------
//: \brief   dtor
//: \deatils
//: \return  None
//: ----------------------------------------------------------------------------
acl::~acl(void)
{
#define _DELETE_OBJ(_obj) if(_obj) { delete _obj; _obj = NULL; }

        _DELETE_OBJ(m_ip_whitelist);
        _DELETE_OBJ(m_ip_accesslist);
        _DELETE_OBJ(m_ip_blacklist);
        _DELETE_OBJ(m_url_rx_whitelist);
        _DELETE_OBJ(m_url_rx_accesslist);
        _DELETE_OBJ(m_url_rx_blacklist);
        _DELETE_OBJ(m_ua_rx_whitelist);
        _DELETE_OBJ(m_ua_rx_accesslist);
        _DELETE_OBJ(m_ua_rx_blacklist);
        _DELETE_OBJ(m_referer_rx_whitelist);
        _DELETE_OBJ(m_referer_rx_accesslist);
        _DELETE_OBJ(m_referer_rx_blacklist);
        _DELETE_OBJ(m_cookie_rx_whitelist);
        _DELETE_OBJ(m_cookie_rx_accesslist);
        _DELETE_OBJ(m_cookie_rx_blacklist);
        if(m_pb) { delete m_pb; m_pb = NULL; }
}
//: ----------------------------------------------------------------------------
//: \details TODO
//: \return  TODO
//: \param   TODO
//: ----------------------------------------------------------------------------
int32_t acl::load(const char *a_buf, uint32_t a_buf_len)
{
        if(!a_buf)
        {
                return WAFLZ_STATUS_ERROR;
        }
        if(a_buf_len > _CONFIG_ACL_MAX_SIZE)
        {
                WAFLZ_PERROR(m_err_msg, "config file size(%u) > max size(%u)",
                             a_buf_len,
                             _CONFIG_ACL_MAX_SIZE);
                return WAFLZ_STATUS_ERROR;
        }
        m_init = false;
        if(m_pb)
        {
                delete m_pb;
                m_pb = NULL;
        }
        m_pb = new waflz_pb::acl();
        // -------------------------------------------------
        // load from json
        // -------------------------------------------------
        int32_t l_s;
        l_s = update_from_json(*m_pb, a_buf, a_buf_len);
        //NDBG_PRINT("whole config %s", m_pb->DebugString().c_str());
        if(l_s != JSPB_OK)
        {
                WAFLZ_PERROR(m_err_msg, "%s", get_jspb_err_msg());
                return WAFLZ_STATUS_ERROR;
        }
        // -------------------------------------------------
        // init
        // -------------------------------------------------
        l_s = init();
        if(l_s != WAFLZ_STATUS_OK)
        {
                return WAFLZ_STATUS_ERROR;
        }
        return WAFLZ_STATUS_OK;
}
//: ----------------------------------------------------------------------------
//: \details TODO
//: \return  TODO
//: \param   TODO
//: ----------------------------------------------------------------------------
int32_t acl::load(const waflz_pb::acl* a_pb)
{
        if(!a_pb)
        {
                WAFLZ_PERROR(m_err_msg, "a_pb == NULL");
                return WAFLZ_STATUS_ERROR;
        }
        // -------------------------------------------------
        // copy from json
        // -------------------------------------------------
        m_pb->CopyFrom(*a_pb);
        // -------------------------------------------------
        // init
        // -------------------------------------------------
        int32_t l_s;
        l_s = init();
        if(l_s != WAFLZ_STATUS_OK)
        {
                return WAFLZ_STATUS_ERROR;
        }
        return WAFLZ_STATUS_OK;
}
//: ----------------------------------------------------------------------------
//: \details TODO
//: \return  TODO
//: \param   TODO
//: ----------------------------------------------------------------------------
int32_t acl::load(void* a_js)
{
        const rapidjson::Document &l_js = *((rapidjson::Document *)a_js);
        int32_t l_s;
        if(m_pb)
        {
                delete m_pb;
                m_pb = NULL;
        }
        m_pb = new waflz_pb::acl();
        l_s = update_from_json(*m_pb, l_js);
        if(l_s != JSPB_OK)
        {
                WAFLZ_PERROR(m_err_msg, "parsing json. Reason: %s", get_jspb_err_msg());
                return WAFLZ_STATUS_ERROR;
        }
        l_s = init();
        if(l_s != WAFLZ_STATUS_OK)
        {
                return WAFLZ_STATUS_ERROR;
        }
        return WAFLZ_STATUS_OK;
}

//: ----------------------------------------------------------------------------
//: \details TODO
//: \return  TODO
//: \param   TODO
//: ----------------------------------------------------------------------------
static int32_t compile_regex_list(regex **ao_regex,
                                  const ::google::protobuf::RepeatedPtrField< ::std::string>& a_list,
                                  uint32_t a_list_len)
{
        if(!ao_regex)
        {
                return WAFLZ_STATUS_ERROR;
        }
        *ao_regex = NULL;
        if(!a_list_len)
        {
                return WAFLZ_STATUS_OK;
        }
        // create regex string
        std::string l_rx;
        l_rx = "(";
        typedef ::google::protobuf::RepeatedPtrField< ::std::string> gpb_list_t;
        uint32_t i_idx = 0;
        for(gpb_list_t::const_iterator i_s = a_list.begin();
            i_s != a_list.end();
            ++i_s, ++i_idx)
        {
                l_rx += *i_s;
                if((i_idx+1) < a_list_len)
                {
                        l_rx += "|";
                }
        }
        l_rx += ")";
        regex *l_pcre = new regex();
        int32_t l_s;
        l_s = l_pcre->init(l_rx.c_str(), l_rx.length());
        if(l_s != WAFLZ_STATUS_OK)
        {
                // TODO -more info
                //WAFLZ_PERROR(m_err_msg, "compiling url whitelist");
                if(l_pcre)
                {
                        delete l_pcre;
                        l_pcre = NULL;
                }
                return WAFLZ_STATUS_ERROR;
        }
        *ao_regex = l_pcre;
        return WAFLZ_STATUS_OK;
}
//: ----------------------------------------------------------------------------
//: \details TODO
//: \return  TODO
//: \param   TODO
//: ----------------------------------------------------------------------------
int32_t acl::init()
{
        if(m_init)
        {
                return WAFLZ_STATUS_OK;
        }
        // -------------------------------------------------
        // acl: ip
        // -------------------------------------------------
        // "ip":
        // {
        //     "whitelist": ["192.168.1.1", "192.168.2.1"],
        //     "blacklist": ["8.8.8.8"]
        // },
        // -------------------------------------------------
        m_id = m_pb->id();
        m_cust_id = m_pb->customer_id();
        m_name = m_pb->name();
        if(m_pb->has_ip())
        {
#define _COMPILE_IP_LIST(_type) do { \
        if(m_pb->ip()._type##_size()) { \
                if(m_ip_##_type) { delete m_ip_##_type; m_ip_##_type = NULL; } \
                m_ip_##_type = new nms(); \
                for(int32_t i_ip = 0; i_ip < m_pb->ip()._type##_size(); ++i_ip) { \
                        const std::string &l_str = m_pb->ip()._type(i_ip); \
                        m_ip_##_type->add(l_str.c_str(), l_str.length()); \
        } } } while(0)
                _COMPILE_IP_LIST(whitelist);
                _COMPILE_IP_LIST(accesslist);
                _COMPILE_IP_LIST(blacklist);
        }
        // -------------------------------------------------
        // country
        // -------------------------------------------------
        //         "country":
        //         {
        //             "whitelist": ["US","CA"],
        //             "blacklist": ["RU", "CN"]
        //         },
        // -------------------------------------------------
        if(m_pb->has_country())
        {
#define _COMPILE_COUNTRY_LIST(_type) do { \
        for(int32_t i_ip = 0; i_ip < m_pb->country()._type##_size(); ++i_ip) { \
                m_country_##_type.insert(m_pb->country()._type(i_ip)); \
        } } while(0)
                _COMPILE_COUNTRY_LIST(whitelist);
                _COMPILE_COUNTRY_LIST(accesslist);
                _COMPILE_COUNTRY_LIST(blacklist);
        }
        // -------------------------------------------------
        // ASN
        // -------------------------------------------------
        if(m_pb->has_asn())
        {
#define _COMPILE_ASN_LIST(_type) do { \
        for(int32_t i_ip = 0; i_ip < m_pb->asn()._type##_size(); ++i_ip) { \
                m_asn_##_type.insert(m_pb->asn()._type(i_ip)); \
        } } while(0)
                _COMPILE_ASN_LIST(whitelist);
                _COMPILE_ASN_LIST(accesslist);
                _COMPILE_ASN_LIST(blacklist);
        }
        // -------------------------------------------------
        // url
        // -------------------------------------------------
        if(m_pb->has_url())
        {
#define _COMPILE_URL_LIST(_type) do { \
        if(m_pb->url()._type##_size()) { \
                int32_t l_s; \
                l_s = compile_regex_list(&m_url_rx_##_type, \
                                         m_pb->url()._type(), \
                                         m_pb->url()._type##_size()); \
                if(l_s != WAFLZ_STATUS_OK) { \
                        WAFLZ_PERROR(m_err_msg, "compiling url %s", #_type); \
                        return WAFLZ_STATUS_ERROR; \
        } } } while(0)
                _COMPILE_URL_LIST(whitelist);
                _COMPILE_URL_LIST(accesslist);
                _COMPILE_URL_LIST(blacklist);
        }
        // -------------------------------------------------
        // user-agent
        // -------------------------------------------------
        if(m_pb->has_user_agent())
        {
#define _COMPILE_USER_AGENT_LIST(_type) do { \
        if(m_pb->user_agent()._type##_size()) { \
                int32_t l_s; \
                l_s = compile_regex_list(&m_ua_rx_##_type, \
                                         m_pb->user_agent()._type(), \
                                         m_pb->user_agent()._type##_size()); \
                if(l_s != WAFLZ_STATUS_OK) { \
                        WAFLZ_PERROR(m_err_msg, "compiling user-agent %s", #_type); \
                        return WAFLZ_STATUS_ERROR; \
        } } } while(0)
                _COMPILE_USER_AGENT_LIST(whitelist);
                _COMPILE_USER_AGENT_LIST(accesslist);
                _COMPILE_USER_AGENT_LIST(blacklist);
        }
        // -------------------------------------------------
        // referer
        // -------------------------------------------------
        if(m_pb->has_referer())
        {
#define _COMPILE_REFERER_LIST(_type) do { \
        if(m_pb->referer()._type##_size()) { \
                int32_t l_s; \
                l_s = compile_regex_list(&m_referer_rx_##_type, \
                                         m_pb->referer()._type(), \
                                         m_pb->referer()._type##_size()); \
                if(l_s != WAFLZ_STATUS_OK) { \
                        WAFLZ_PERROR(m_err_msg, "compiling referer %s", #_type); \
                        return WAFLZ_STATUS_ERROR; \
        } } } while(0)
                _COMPILE_REFERER_LIST(whitelist);
                _COMPILE_REFERER_LIST(accesslist);
                _COMPILE_REFERER_LIST(blacklist);
        }
        // -------------------------------------------------
        // cookie
        // -------------------------------------------------
        if(m_pb->has_cookie())
        {
#define _COMPILE_COOKIE_LIST(_type) do { \
        if(m_pb->cookie()._type##_size()) { \
                int32_t l_s; \
                l_s = compile_regex_list(&m_cookie_rx_##_type, \
                                         m_pb->cookie()._type(), \
                                         m_pb->cookie()._type##_size()); \
                if(l_s != WAFLZ_STATUS_OK) { \
                        WAFLZ_PERROR(m_err_msg, "compiling cookie %s", #_type); \
                        return WAFLZ_STATUS_ERROR; \
        } } } while(0)
                _COMPILE_COOKIE_LIST(whitelist);
                _COMPILE_COOKIE_LIST(accesslist);
                _COMPILE_COOKIE_LIST(blacklist);
        }
        // -------------------------------------------------
        // allowed_http_methods
        // -------------------------------------------------
        if(m_pb->allowed_http_methods_size())
        {
                for(int32_t i_t = 0; i_t < m_pb->allowed_http_methods_size(); ++i_t)
                {
                        m_allowed_http_methods.insert(m_pb->allowed_http_methods(i_t));
                }
        }
        // -------------------------------------------------
        // allowed_http_versions
        // -------------------------------------------------
        if(m_pb->allowed_http_versions_size())
        {
                for(int32_t i_t = 0; i_t < m_pb->allowed_http_versions_size(); ++i_t)
                {
                        m_allowed_http_versions.insert(m_pb->allowed_http_versions(i_t));
                }
        }
        // -------------------------------------------------
        // allowed_request_content
        // -------------------------------------------------
        if(m_pb->allowed_request_content_types_size())
        {
                for(int32_t i_t = 0; i_t < m_pb->allowed_request_content_types_size(); ++i_t)
                {
                        m_allowed_request_content_types.insert(m_pb->allowed_request_content_types(i_t));
                }
        }
        // -------------------------------------------------
        // allowed_request_content
        // -------------------------------------------------
        if(m_pb->disallowed_extensions_size())
        {
                for(int32_t i_t = 0; i_t < m_pb->disallowed_extensions_size(); ++i_t)
                {
                        m_disallowed_extensions.insert(m_pb->disallowed_extensions(i_t));
                }
        }
        // -------------------------------------------------
        // disallowed_headers
        // -------------------------------------------------
        if(m_pb->disallowed_headers_size())
        {
                for(int32_t i_t = 0; i_t < m_pb->disallowed_headers_size(); ++i_t)
                {
                        m_disallowed_headers.insert(m_pb->disallowed_headers(i_t));
                }
        }
        m_init = true;
        return WAFLZ_STATUS_OK;
}
//: ----------------------------------------------------------------------------
//: \details TODO
//: \return  TODO
//: \param   TODO
//: ----------------------------------------------------------------------------
int32_t acl::process_whitelist(bool &ao_match, rqst_ctx &a_ctx)
{
        ao_match = false;
        const char *l_buf = NULL;
        uint32_t l_buf_len = 0;
        data_t l_d;
        const data_map_t &l_hm = a_ctx.m_header_map;
        int32_t l_s;
        // -------------------------------------------------
        // ip
        // -------------------------------------------------
        l_buf = a_ctx.m_src_addr.m_data;
        l_buf_len = a_ctx.m_src_addr.m_len;
        if(m_ip_whitelist &&
           l_buf &&
           l_buf_len)
        {
                l_s = m_ip_whitelist->contains(ao_match, l_buf, l_buf_len);
                if(l_s != WAFLZ_STATUS_OK)
                {
                        // TODO log reason???
                        goto country_check;
                }
                // if in whitelist -bail out of modsec processing
                if(ao_match)
                {
                        return WAFLZ_STATUS_OK;
                }
        }
country_check:
        // -------------------------------------------------
        // country
        // -------------------------------------------------
        if(m_country_whitelist.size() &&
           l_buf &&
           l_buf_len &&
           a_ctx.m_geo_cn2.m_data &&
           a_ctx.m_geo_cn2.m_len)
        {
                std::string l_cn_str;
                l_cn_str.assign(a_ctx.m_geo_cn2.m_data, a_ctx.m_geo_cn2.m_len);
                if(m_country_whitelist.find(l_cn_str) != m_country_whitelist.end())
                {
                        ao_match = true;
                        return WAFLZ_STATUS_OK;
                }
        }
        // -------------------------------------------------
        // asn
        // -------------------------------------------------
        if(m_asn_whitelist.size() &&
           a_ctx.m_src_asn)
        {
                if(m_asn_whitelist.find(a_ctx.m_src_asn) != m_asn_whitelist.end())
                {
                        ao_match = true;
                        return WAFLZ_STATUS_OK;
                }
        }
        // -------------------------------------------------
        // get url
        // -------------------------------------------------
        if(!m_url_rx_whitelist)
        {
                goto user_agent_check;
        }
        if(m_url_rx_whitelist &&
           a_ctx.m_uri.m_data &&
           a_ctx.m_uri.m_len)
        {
                int32_t l_s;
                l_s = m_url_rx_whitelist->compare(a_ctx.m_uri.m_data, a_ctx.m_uri.m_len);
                // if failed to match
                if(l_s >= 0)
                {
                        ao_match = true;
                        return WAFLZ_STATUS_OK;
                }
        }
user_agent_check:
        // -------------------------------------------------
        // user-agent
        // -------------------------------------------------
        if(!m_ua_rx_whitelist)
        {
                goto referer_check;
        }
        _GET_HEADER("User-Agent", l_buf);
        if(m_ua_rx_whitelist &&
           l_buf &&
           l_buf_len)
        {
                int32_t l_s;
                l_s = m_ua_rx_whitelist->compare(l_buf, l_buf_len);
                // if failed to match
                if(l_s >= 0)
                {
                        ao_match = true;
                        return WAFLZ_STATUS_OK;
                }
        }
referer_check:
        // -------------------------------------------------
        // referer
        // -------------------------------------------------
        if(!m_referer_rx_whitelist)
        {
                goto cookie_check;
        }
        _GET_HEADER("Referer", l_buf);
        if(m_referer_rx_whitelist &&
           l_buf &&
           l_buf_len)
        {
                int32_t l_s;
                l_s = m_referer_rx_whitelist->compare(l_buf, l_buf_len);
                // if failed to match
                if(l_s >= 0)
                {
                        ao_match = true;
                        return WAFLZ_STATUS_OK;
                }
        }
cookie_check:
        // -------------------------------------------------
        // cookie
        // -------------------------------------------------
        if(!m_cookie_rx_whitelist)
        {
                return WAFLZ_STATUS_OK;
        }
        _GET_HEADER("Cookie", l_buf);
        if(m_cookie_rx_whitelist &&
           l_buf &&
           l_buf_len)
        {
                int32_t l_s;
                l_s =  m_cookie_rx_whitelist->compare(l_buf, l_buf_len);
                if(l_s >= 0)
                {
                        ao_match = true;
                        return WAFLZ_STATUS_OK;
                }
        }
        return WAFLZ_STATUS_OK;
}
//: ----------------------------------------------------------------------------
//: \details TODO
//: \return  TODO
//: \param   TODO
//: ----------------------------------------------------------------------------
int32_t acl::process_accesslist(waflz_pb::event **ao_event, rqst_ctx &a_ctx)
{
        if(!ao_event)
        {
                return WAFLZ_STATUS_ERROR;
        }
        *ao_event = NULL;
        const char *l_buf = NULL;
        uint32_t l_buf_len = 0;
        data_t l_d;
        const data_map_t &l_hm = a_ctx.m_header_map;
        int32_t l_s;
        // -------------------------------------------------
        // ip
        // -------------------------------------------------
        // ip or src_addr used for: ip, country, asn
        l_buf = a_ctx.m_src_addr.m_data;
        l_buf_len = a_ctx.m_src_addr.m_len;
        if(m_ip_accesslist &&
           l_buf &&
           l_buf_len)
        {
                bool l_match = false;
                l_s = m_ip_accesslist->contains(l_match, l_buf, l_buf_len);
                if(l_s != WAFLZ_STATUS_OK)
                {
                        // TODO log error reason???
                        goto country_check;
                }
                if(l_match)
                {
                        goto country_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Accesslist IP match");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80008);
                l_sevent->set_rule_msg("Accesslist IP deny");
                l_sevent->set_rule_op_name("ipMatch");
                l_sevent->set_rule_op_param("ip_accesslist");
                l_sevent->add_rule_tag("ACCESSLIST/IP");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("TX");
                l_rule_target->set_param("REAL_IP");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("TX:real_ip");
                l_var->set_value(l_buf, l_buf_len);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
country_check:
        // -------------------------------------------------
        // country
        // -------------------------------------------------
        if(m_country_accesslist.size() &&
           l_buf &&
           l_buf_len &&
           a_ctx.m_geo_cn2.m_data &&
           a_ctx.m_geo_cn2.m_len)
        {
                std::string l_cn_str;
                l_cn_str.assign(a_ctx.m_geo_cn2.m_data, a_ctx.m_geo_cn2.m_len);
                bool l_match = false;
                if(m_country_accesslist.find(l_cn_str) != m_country_accesslist.end())
                {
                        l_match = true;
                }
                if(l_match)
                {
                        goto asn_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Accesslist Country deny");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80004);
                l_sevent->set_rule_msg("Accesslist Country deny");
                l_sevent->set_rule_op_name("geoLookup");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("ACCESSLIST/COUNTRY");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("TX");
                l_rule_target->set_param("REAL_IP");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("GEO:COUNTRY_CODE");
                l_var->set_value(l_cn_str);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
asn_check:
        // -------------------------------------------------
        // ASN
        // -------------------------------------------------
        if(m_asn_accesslist.size() &&
           a_ctx.m_src_asn)
        {
                bool l_match = false;
                if(m_asn_accesslist.find(a_ctx.m_src_asn) != m_asn_accesslist.end())
                {
                        l_match = true;
                }
                if(l_match)
                {
                        goto url_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Accesslist ASN deny");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80001);
                l_sevent->set_rule_msg("Accesslist ASN deny");
                l_sevent->set_rule_op_name("asnLookup");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("ACCESSLIST/ASN");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("TX");
                l_rule_target->set_param("REAL_IP");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("GEO:ASN");
                char l_asn_str[16];
                snprintf(l_asn_str, 16, "AS%u", a_ctx.m_src_asn);
                l_var->set_value(l_asn_str);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
url_check:
        // -------------------------------------------------
        // url
        // -------------------------------------------------
        if(!m_url_rx_accesslist)
        {
                goto user_agent_check;
        }
        // set buf to uri
        l_buf = a_ctx.m_uri.m_data;
        l_buf_len = a_ctx.m_uri.m_len;
        if(m_url_rx_accesslist &&
           l_buf &&
           l_buf_len)
        {
                int32_t l_s;
                bool l_match = false;
                l_s = m_url_rx_accesslist->compare(l_buf, l_buf_len);
                if(l_s >= 0)
                {
                        l_match = true;
                }
                if(l_match)
                {
                        goto user_agent_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Accesslist URL deny");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80011);
                l_sevent->set_rule_msg("Accesslist URL deny");
                l_sevent->set_rule_op_name("rx");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("ACCESSLIST/URL");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_URI_RAW");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("REQUEST_URI_RAW");
                l_var->set_value(l_buf, l_buf_len);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
user_agent_check:
        // -------------------------------------------------
        // user-agent
        // -------------------------------------------------
        if(!m_ua_rx_accesslist)
        {
                goto referer_check;
        }
        // get header from header map.
        _GET_HEADER("User-Agent", l_buf);
        if(m_ua_rx_accesslist &&
           l_buf &&
           l_buf_len)
        {
                int32_t l_s;
                bool l_match = false;
                std::string l_rx_capture;
                l_s = m_ua_rx_accesslist->compare(l_buf, l_buf_len, &l_rx_capture);
                if(l_s >= 0)
                {
                        l_match = true;
                }
                if(l_match)
                {
                        goto referer_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Accesslist User-Agent deny");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80012);
                l_sevent->set_rule_msg("Accesslist User-Agent deny");
                l_sevent->set_rule_op_name("rx");
                l_sevent->set_rule_op_param(m_ua_rx_accesslist->get_regex_string());
                l_sevent->add_rule_tag("ACCESSLIST/USER-AGENT");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_HEADERS");
                l_rule_target->set_param("User-Agent");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("REQUEST_HEADERS:User-Agent");
                l_var->set_value(l_buf, l_buf_len);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
referer_check:
        // -------------------------------------------------
        // referer
        // -------------------------------------------------
        if(!m_referer_rx_accesslist)
        {
                goto cookie_check;
        }
        _GET_HEADER("Referer", l_buf);
        if(m_referer_rx_accesslist &&
           l_buf &&
           l_buf_len)
        {
                int32_t l_s;
                bool l_match = false;
                std::string l_rx_capture;
                l_s = m_referer_rx_accesslist->compare(l_buf, l_buf_len, &l_rx_capture);
                if(l_s >= 0)
                {
                        l_match = true;
                }
                if(l_match)
                {
                        goto cookie_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Accesslist Referer deny");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80010);
                l_sevent->set_rule_msg("Accesslist Referer deny");
                l_sevent->set_rule_op_name("rx");
                l_sevent->set_rule_op_param(m_referer_rx_accesslist->get_regex_string());
                l_sevent->add_rule_tag("ACCESSLIST/REFERER");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_HEADERS");
                l_rule_target->set_value("Referer");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("REQUEST_HEADERS:Referer");
                l_var->set_value(l_buf, l_buf_len);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
cookie_check:
        // -------------------------------------------------
        // cookie
        // -------------------------------------------------
        if(!m_cookie_rx_accesslist)
        {
                return WAFLZ_STATUS_OK;
        }
        _GET_HEADER("Cookie", l_buf);
        if(m_cookie_rx_accesslist &&
           l_buf &&
           l_buf_len)
        {
                int32_t l_s;
                bool l_match = false;
                std::string l_rx_capture;
                l_s = m_cookie_rx_accesslist->compare(l_buf, l_buf_len, &l_rx_capture);
                if(l_s >= 0)
                {
                        l_match = true;
                }
                if(l_match)
                {
                        goto done;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Accesslist Cookie deny");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80003);
                l_sevent->set_rule_msg("Accesslist Cookie deny");
                l_sevent->set_rule_op_name("rx");
                l_sevent->set_rule_op_param(m_cookie_rx_accesslist->get_regex_string());
                l_sevent->add_rule_tag("ACCESSLIST/Cookie");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_HEADERS");
                l_rule_target->set_value("Cookie");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("REQUEST_HEADERS:Cookie");
                l_var->set_value(l_buf, l_buf_len);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
done:
        return WAFLZ_STATUS_OK;
}
//: ----------------------------------------------------------------------------
//: \details TODO
//: \return  TODO
//: \param   TODO
//: ----------------------------------------------------------------------------
int32_t acl::process_blacklist(waflz_pb::event **ao_event, rqst_ctx &a_ctx)
{
        if(!ao_event)
        {
                return WAFLZ_STATUS_ERROR;
        }
        *ao_event = NULL;
        const char *l_buf = NULL;
        uint32_t l_buf_len = 0;
        data_t l_d;
        const data_map_t &l_hm = a_ctx.m_header_map;
        int32_t l_s;
        // -------------------------------------------------
        // ip
        // -------------------------------------------------
        // ip or src_addr used for: ip, country, asn
        l_buf = a_ctx.m_src_addr.m_data;
        l_buf_len = a_ctx.m_src_addr.m_len;
        if(m_ip_blacklist &&
           l_buf &&
           l_buf_len)
        {
                bool l_match = false;
                l_s = m_ip_blacklist->contains(l_match, l_buf, l_buf_len);
                if(l_s != WAFLZ_STATUS_OK)
                {
                        // TODO log error reason???
                        goto country_check;
                }
                if(!l_match)
                {
                        goto country_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Blacklist IP match");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80008);
                l_sevent->set_rule_msg("Blacklist IP match");
                l_sevent->set_rule_op_name("ipMatch");
                l_sevent->set_rule_op_param("ip_blacklist");
                l_sevent->add_rule_tag("BLACKLIST/IP");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("TX");
                l_rule_target->set_param("REAL_IP");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("TX:real_ip");
                l_var->set_value(l_buf, l_buf_len);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
country_check:
        // -------------------------------------------------
        // country
        // -------------------------------------------------
        if(m_country_blacklist.size() &&
           l_buf &&
           l_buf_len &&
           a_ctx.m_geo_cn2.m_data &&
           a_ctx.m_geo_cn2.m_len)
        {
                std::string l_cn_str;
                l_cn_str.assign(a_ctx.m_geo_cn2.m_data, a_ctx.m_geo_cn2.m_len);
                bool l_match = false;
                if(m_country_blacklist.find(l_cn_str) != m_country_blacklist.end())
                {
                        l_match = true;
                }
                if(!l_match)
                {
                        goto asn_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Blacklist Country match");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80004);
                l_sevent->set_rule_msg("Blacklist Country match");
                l_sevent->set_rule_op_name("geoLookup");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("BLACKLIST/COUNTRY");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("TX");
                l_rule_target->set_param("REAL_IP");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("GEO:COUNTRY_CODE");
                l_var->set_value(l_cn_str);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
asn_check:
        // -------------------------------------------------
        // ASN
        // -------------------------------------------------
        if(m_asn_blacklist.size() &&
           a_ctx.m_src_asn)
        {
                bool l_match = false;
                if(m_asn_blacklist.find(a_ctx.m_src_asn) != m_asn_blacklist.end())
                {
                        l_match = true;
                }
                if(!l_match)
                {
                        goto url_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Blacklist ASN match");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80001);
                l_sevent->set_rule_msg("Blacklist ASN match");
                l_sevent->set_rule_op_name("asnLookup");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("BLACKLIST/ASN");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("TX");
                l_rule_target->set_param("REAL_IP");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("GEO:ASN");
                char l_asn_str[16];
                snprintf(l_asn_str, 16, "AS%u", a_ctx.m_src_asn);
                l_var->set_value(l_asn_str);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
url_check:
        // -------------------------------------------------
        // url
        // -------------------------------------------------
        if(!m_url_rx_blacklist)
        {
                goto user_agent_check;
        }
        // set buf to uri
        l_buf = a_ctx.m_uri.m_data;
        l_buf_len = a_ctx.m_uri.m_len;
        if(m_url_rx_blacklist &&
           l_buf &&
           l_buf_len)
        {
                int32_t l_s;
                bool l_match = false;
                l_s = m_url_rx_blacklist->compare(l_buf, l_buf_len);
                if(l_s >= 0)
                {
                        l_match = true;
                }
                if(!l_match)
                {
                        goto user_agent_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Blacklist URL match");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80011);
                l_sevent->set_rule_msg("Blacklist URL match");
                l_sevent->set_rule_op_name("rx");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("BLACKLIST/URL");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_URI_RAW");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("REQUEST_URI_RAW");
                l_var->set_value(l_buf, l_buf_len);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
user_agent_check:
        // -------------------------------------------------
        // user-agent
        // -------------------------------------------------
        if(!m_ua_rx_blacklist)
        {
                goto referer_check;
        }
        // get header from header map.
        _GET_HEADER("User-Agent", l_buf);
        if(m_ua_rx_blacklist &&
           l_buf &&
           l_buf_len)
        {
                int32_t l_s;
                bool l_match = false;
                std::string l_rx_capture;
                l_s = m_ua_rx_blacklist->compare(l_buf, l_buf_len, &l_rx_capture);
                if(l_s >= 0)
                {
                        l_match = true;
                }
                if(!l_match)
                {
                        goto referer_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Blacklist User-Agent match");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80012);
                l_sevent->set_rule_msg("Blacklist User-Agent match");
                l_sevent->set_rule_op_name("rx");
                l_sevent->set_rule_op_param(m_ua_rx_blacklist->get_regex_string());
                l_sevent->add_rule_tag("BLACKLIST/USER-AGENT");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_HEADERS");
                l_rule_target->set_param("User-Agent");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("REQUEST_HEADERS:User-Agent");
                l_var->set_value(l_buf, l_buf_len);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
referer_check:
        // -------------------------------------------------
        // referer
        // -------------------------------------------------
        if(!m_referer_rx_blacklist)
        {
                goto cookie_check;
        }
        _GET_HEADER("Referer", l_buf);
        if(m_referer_rx_blacklist &&
           l_buf &&
           l_buf_len)
        {
                int32_t l_s;
                bool l_match = false;
                std::string l_rx_capture;
                l_s = m_referer_rx_blacklist->compare(l_buf, l_buf_len, &l_rx_capture);
                if(l_s >= 0)
                {
                        l_match = true;
                }
                if(!l_match)
                {
                        goto cookie_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Blacklist Referer match");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80010);
                l_sevent->set_rule_msg("Blacklist Referer match");
                l_sevent->set_rule_op_name("rx");
                l_sevent->set_rule_op_param(m_referer_rx_blacklist->get_regex_string());
                l_sevent->add_rule_tag("BLACKLIST/REFERER");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_HEADERS");
                l_rule_target->set_value("Referer");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("REQUEST_HEADERS:Referer");
                l_var->set_value(l_buf, l_buf_len);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
cookie_check:
        // -------------------------------------------------
        // cookie
        // -------------------------------------------------
        if(!m_cookie_rx_blacklist)
        {
                return WAFLZ_STATUS_OK;
        }
        _GET_HEADER("Cookie", l_buf);
        if(m_cookie_rx_blacklist &&
           l_buf &&
           l_buf_len)
        {
                int32_t l_s;
                bool l_match = false;
                std::string l_rx_capture;
                l_s = m_cookie_rx_blacklist->compare(l_buf, l_buf_len, &l_rx_capture);
                if(l_s >= 0)
                {
                        l_match = true;
                }
                if(!l_match)
                {
                        goto done;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Blacklist Cookie match");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80003);
                l_sevent->set_rule_msg("Blacklist Cookie match");
                l_sevent->set_rule_op_name("rx");
                l_sevent->set_rule_op_param(m_cookie_rx_blacklist->get_regex_string());
                l_sevent->add_rule_tag("BLACKLIST/Cookie");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_HEADERS");
                l_rule_target->set_value("Cookie");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("REQUEST_HEADERS:Cookie");
                l_var->set_value(l_buf, l_buf_len);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
done:
        return WAFLZ_STATUS_OK;
}
//: ----------------------------------------------------------------------------
//: \details TODO
//: \return  TODO
//: \param   TODO
//: ----------------------------------------------------------------------------
int32_t acl::process_settings(waflz_pb::event **ao_event, rqst_ctx &a_ctx)
{
        if(!ao_event)
        {
                return WAFLZ_STATUS_ERROR;
        }
        *ao_event = NULL;
        // -------------------------------------------------
        // file size check
        // -------------------------------------------------
        if(m_pb->has_max_file_size())
        {
                // -----------------------------------------
                // get length from content-length header
                // -----------------------------------------
                const char *l_buf = NULL;
                uint32_t l_buf_len = 0;
                unsigned long l_cl = 0;
                data_t l_d;
                const data_map_t &l_hm = a_ctx.m_header_map;
                _GET_HEADER("Content-Length", l_buf);
                if(!l_buf ||
                   !l_buf_len)
                {
                        goto method_check;
                }
                l_cl = strntoul(l_buf, l_buf_len, NULL, 10);
                if(l_cl == ULONG_MAX)
                {
                        goto method_check;
                }
                if(l_cl <= 0)
                {
                        goto method_check;
                }
                if(l_cl < m_pb->max_file_size())
                {
                        // file size within limits
                        goto method_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Uploaded file size too large");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80006);
                l_sevent->set_rule_msg("Uploaded file size too large");
                l_sevent->set_rule_op_name("");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("HTTP POLICY");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_HEADERS");
                l_rule_target->set_param("max_file_size");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("Content-Length");
                l_var->set_value(l_buf);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
method_check:
        // -------------------------------------------------
        // http methods
        // -------------------------------------------------
        if(!m_allowed_http_methods.size())
        {
                goto content_type_check;
        }
        if(a_ctx.m_method.m_data &&
           a_ctx.m_method.m_len)
        {
                // Look for method in allowed m set
                std::string l_method(a_ctx.m_method.m_data, a_ctx.m_method.m_len);
                if(m_allowed_http_methods.find(l_method) != m_allowed_http_methods.end())
                {
                        // Found the method in allowed list
                        goto content_type_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Method is not allowed by policy");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_event->set_rule_msg("Method is not allowed by policy");
                l_sevent->set_rule_id(80009);
                l_sevent->set_rule_msg("Method is not allowed by policy");
                l_sevent->set_rule_op_name("");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("HTTP POLICY");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_METHOD");
                l_rule_target->set_param("allowed_http_methods");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("REQUEST_METHOD");
                l_var->set_value(a_ctx.m_method.m_data);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
content_type_check:
        // -------------------------------------------------
        // Request Content Type
        // -------------------------------------------------
        if(!m_allowed_request_content_types.size())
        {
                goto file_ext_check;
        }
        // -------------------------------------------------
        // skip inspection for idempotent methods
        // -------------------------------------------------
        if(a_ctx.m_method.m_data &&
           a_ctx.m_method.m_len)
        {
                std::string l_method(a_ctx.m_method.m_data, a_ctx.m_method.m_len);
                if(g_ignore_ct_set.find(l_method) != g_ignore_ct_set.end())
                {
                        goto file_ext_check;
                }
        }
        // -------------------------------------------------
        // foreach content type...
        // -------------------------------------------------
        for(data_list_t::const_iterator i_h = a_ctx.m_content_type_list.begin();
            i_h != a_ctx.m_content_type_list.end();
            ++i_h)
        {
                std::string l_cont_type(i_h->m_data, i_h->m_len);
                // -----------------------------------------
                // if any content type matches allowed skip
                // rest of list -pass thru
                // -----------------------------------------
                if(m_allowed_request_content_types.find(l_cont_type) != m_allowed_request_content_types.end())
                {
                           goto file_ext_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Request content type is not allowed by policy");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80002);
                l_sevent->set_rule_msg("Request content type is not allowed by policy");
                l_sevent->set_rule_op_name("");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("HTTP POLICY");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_HEADERS");
                l_rule_target->set_param("allowed_request_content_types");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("Content-Type");
                l_var->set_value(l_cont_type);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
file_ext_check:
        // -------------------------------------------------
        // disallowed extensions
        // -------------------------------------------------
        if(!m_disallowed_extensions.size())
        {
                goto header_check;
        }
        if(m_disallowed_extensions.size() &&
           a_ctx.m_file_ext.m_data &&
           a_ctx.m_file_ext.m_len)
        {
                std::string l_file_ext(a_ctx.m_file_ext.m_data, a_ctx.m_file_ext.m_len);
                // unlike previous checks, extension shouldnt be in list, hence ==
                if(m_disallowed_extensions.find(l_file_ext) == m_disallowed_extensions.end())
                {
                        // extension not found in disallowed list
                        goto header_check;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("File extension is not allowed by policy");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80005);
                l_sevent->set_rule_msg("File extension is not allowed by policy");
                l_sevent->set_rule_op_name("");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("HTTP POLICY");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("FILE_EXT");
                l_rule_target->set_param("disallowed_extensions");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("FILE_EXT");
                l_var->set_value(l_file_ext);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
header_check:
        // -------------------------------------------------
        // disallowed headers
        // -------------------------------------------------
        if(!m_disallowed_headers.size() ||
           !a_ctx.m_header_list.size())
        {
                return WAFLZ_STATUS_OK;
        }
        for(const_arg_list_t::const_iterator i_h = a_ctx.m_header_list.begin();
            i_h != a_ctx.m_header_list.end();
            ++i_h)
        {
                // similar to previous check, ==
                if(m_disallowed_headers.find(i_h->m_key) == m_disallowed_headers.end())
                {
                        continue;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Request header is not allowed by policy");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(80007);
                l_sevent->set_rule_msg("Request header is not allowed by policy");
                l_sevent->set_rule_op_name("");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("HTTP POLICY");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_HEADERS");
                l_rule_target->set_param("disallowed_headers");
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("REQUEST_HEADERS");
                l_var->set_value(i_h->m_key);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
#if 0
version_check:
        // -------------------------------------------------
        // allowed_http_versions
        // -------------------------------------------------
        if(m_allowed_http_versions.size() &&
           l_buf &&
           l_buf_len)
        {
                bool l_match = false;
                if(m_allowed_http_versions.find(l_buf) != m_allowed_http_versions.end())
                {
                        l_match = true;
                }
                if(!l_match)
                {
                        goto done;
                }
                // -----------------------------------------
                // top level event
                // -----------------------------------------
                waflz_pb::event *l_event = new ::waflz_pb::event();
                l_event->set_rule_msg("Method is not allowed by policy");
                // -----------------------------------------
                // subevent
                // -----------------------------------------
                ::waflz_pb::event *l_sevent = l_event->add_sub_event();
                l_sevent->set_rule_id(430425);
                l_sevent->set_rule_msg("HTTP protocol version is not allowed by policy");
                l_sevent->set_rule_op_name("");
                l_sevent->set_rule_op_param("");
                l_sevent->add_rule_tag("HTTP POLICY");
                ::waflz_pb::event_var_t* l_rule_target = l_sevent->add_rule_target();
                l_rule_target->set_name("REQUEST_PROTOCOL");
                l_rule_target->set_param(l_buf);
                ::waflz_pb::event_var_t* l_var = l_sevent->mutable_matched_var();
                l_var->set_name("REQUEST_PROTOCOL");
                l_var->set_value(l_buf);
                *ao_event = l_event;
                return WAFLZ_STATUS_OK;
        }
done:
#endif
        return WAFLZ_STATUS_OK;
}
//: ----------------------------------------------------------------------------
//: \details TODO
//: \return  TODO
//: \param   TODO
//: ----------------------------------------------------------------------------
int32_t acl::process(waflz_pb::event **ao_event,
                     bool &ao_whitelist,
                     void *a_ctx,
                     rqst_ctx &a_rqst_ctx)
{
        if(!ao_event)
        {
                return WAFLZ_STATUS_ERROR;
        }
        if(!a_rqst_ctx.m_init_phase_1)
        {
                return WAFLZ_STATUS_ERROR;
        }
        *ao_event = NULL;
        ao_whitelist = false;
        bool l_match = false;
        int32_t l_s;
        // -------------------------------------------------
        // whitelist...
        // -------------------------------------------------
        l_s = process_whitelist(l_match, a_rqst_ctx);
        if(l_s != WAFLZ_STATUS_OK)
        {
                return WAFLZ_STATUS_ERROR;
        }
        // if whitelist match, we outtie
        if(l_match)
        {
                ao_whitelist = true;
                return WAFLZ_STATUS_OK;
        }
        waflz_pb::event *l_event = NULL;
        // -------------------------------------------------
        // accesslist...
        // -------------------------------------------------
        l_s = process_accesslist(&l_event, a_rqst_ctx);
        if(l_s != WAFLZ_STATUS_OK)
        {
                return WAFLZ_STATUS_ERROR;
        }
        if(l_event)
        {
                goto done;
        }
        // -------------------------------------------------
        // blacklist...
        // -------------------------------------------------
        l_s = process_blacklist(&l_event, a_rqst_ctx);
        if(l_s != WAFLZ_STATUS_OK)
        {
                return WAFLZ_STATUS_ERROR;
        }
        if(l_event)
        {
                goto done;
        }
        // -------------------------------------------------
        // settings...
        // -------------------------------------------------
        l_s = process_settings(&l_event, a_rqst_ctx);
        if(l_s != WAFLZ_STATUS_OK)
        {
                return WAFLZ_STATUS_ERROR;
        }
        if(l_event)
        {
                goto done;
        }
done:
        // -------------------------------------------------
        // Set config properties
        // -------------------------------------------------
        if(l_event)
        {
                l_event->set_acl_config_id(m_id);
                l_event->set_acl_config_name(m_name);
        }
        *ao_event = l_event;
        // -------------------------------------------------
        // cleanup
        // -------------------------------------------------
        return WAFLZ_STATUS_OK;
}
}
