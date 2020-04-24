//: ----------------------------------------------------------------------------
//: Copyright (C) 2016 Verizon.  All Rights Reserved.
//: All Rights Reserved
//:
//: \file:    profile.h
//: \details: TODO
//: \author:  Reed P. Morrison
//: \date:    04/15/2016
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
#ifndef _PROFILE_H_
#define _PROFILE_H_
//: ----------------------------------------------------------------------------
//: includes
//: ----------------------------------------------------------------------------
#include "waflz/def.h"
#include <string>
#include <list>
#include <set>
#include <strings.h>
#include <map>
//: ----------------------------------------------------------------------------
//: fwd decl's
//: ----------------------------------------------------------------------------
namespace waflz_pb {
class enforcement;
class profile;
class event;
class request_info;
class acl;
}
namespace ns_waflz {
//: ----------------------------------------------------------------------------
//: fwd decl's
//: ----------------------------------------------------------------------------
class engine;
class waf;
class acl;
class regex;
class rqst_ctx;
//: ----------------------------------------------------------------------------
//: types
//: ----------------------------------------------------------------------------
typedef std::list<regex *> pcre_list_t;
typedef std::list <std::string> str_list_t;
//: ----------------------------------------------------------------------------
//: TODO
//: ----------------------------------------------------------------------------
class profile
{
public:
        // -------------------------------------------------
        // public methods
        // -------------------------------------------------
        profile(engine &a_engine);
        ~profile();
        int32_t process(waflz_pb::event **ao_event, void *a_ctx, part_mk_t a_part_mk, rqst_ctx **ao_rqst_ctx = NULL);
        int32_t load(const char *a_buf, uint32_t a_buf_len);
        int32_t load(void* a_js);
        int32_t load(const waflz_pb::profile *a_pb);
        //: ------------------------------------------------
        //:               G E T T E R S
        //: ------------------------------------------------
        //: ------------------------------------------------
        //: \details Get last error message string
        //: \return  last error message (in buffer)
        //: ------------------------------------------------
        const char *get_err_msg(void) { return m_err_msg; }
        waflz_pb::profile *get_pb(void) { return m_pb; }
        waf *get_waf(void) { return m_waf; }
        const std::string& get_id(void) { return m_id; }
        const std::string& get_cust_id(void) { return m_cust_id; }
        const std::string& get_name(void) { return m_name; }
        const std::string& get_resp_header_name(void) { return m_resp_header_name; }
        uint16_t get_action(void) { return m_action; }
        void set_pb(waflz_pb::profile *a_pb);
private:
        // -------------------------------------------------
        // private types
        // -------------------------------------------------
        typedef std::list<regex *> regex_list_t;
        // -------------------------------------------------
        // private methods
        // -------------------------------------------------
        //DISALLOW_DEFAULT_CTOR(profile);
        // disallow copy/assign
        profile(const profile &);
        profile& operator=(const profile &);
        int32_t regex_list_add(const std::string &a_regex, pcre_list_t &a_pcre_list);
        int32_t init(void);
        int32_t validate(void);
        // -------------------------------------------------
        // private members
        // -------------------------------------------------
        bool m_init;
        waflz_pb::profile *m_pb;
        char m_err_msg[WAFLZ_ERR_LEN];
        engine &m_engine;
        // -------------------------------------------------
        // engines...
        // -------------------------------------------------
        acl *m_acl;
        waf *m_waf;
        // -------------------------------------------------
        // properties
        // -------------------------------------------------
        std::string m_id;
        std::string m_cust_id;
        std::string m_name;
        std::string m_resp_header_name;
        uint16_t m_action;
        uint32_t m_owasp_ruleset_version;
        uint32_t m_paranoia_level;
        pcre_list_t m_il_query;
        pcre_list_t m_il_header;
        pcre_list_t m_il_cookie;
        // -------------------------------------------------
        // friends
        // -------------------------------------------------
        friend class instance;
        friend class acl;
};
}
#endif
