#!/usr/bin/env python3
'''Test WAF Access settings'''
#TODO: make so waflz_server only runs once and then can post to it
# ------------------------------------------------------------------------------
# Imports
# ------------------------------------------------------------------------------
import pytest
import subprocess
import os
import sys
import json
import time
import requests
# ------------------------------------------------------------------------------
# Constants
# ------------------------------------------------------------------------------
G_TEST_HOST = 'http://127.0.0.1:12345/'
# ------------------------------------------------------------------------------
# globals
# ------------------------------------------------------------------------------
g_server_pid = -1
# ------------------------------------------------------------------------------
#
# ------------------------------------------------------------------------------
def run_command(command):
    p = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = p.communicate()
    return (p.returncode, stdout, stderr)
# ------------------------------------------------------------------------------
# fixture
# ------------------------------------------------------------------------------
@pytest.fixture(scope='module')
def setup_waflz_server():
    # ------------------------------------------------------
    # setup
    # ------------------------------------------------------
    l_cwd = os.getcwd()
    l_file_path = os.path.dirname(os.path.abspath(__file__))
    l_ruleset_path = os.path.realpath(os.path.join(l_file_path, '../../data/waf/ruleset'))
    l_geoip2city_path = os.path.realpath(os.path.join(l_file_path, '../../data/waf/db/GeoLite2-City.mmdb'))
    l_geoip2ISP_path = os.path.realpath(os.path.join(l_file_path, '../../data/waf/db/GeoLite2-ASN.mmdb'))
    l_profile_path = os.path.realpath(os.path.join(l_file_path, 'test_bb_acl.waf.prof.json'))
    l_waflz_server_path = os.path.abspath(os.path.join(l_file_path, '../../../build/util/waflz_server/waflz_server'))
    l_subproc = subprocess.Popen([l_waflz_server_path,
                                  '-f', l_profile_path,
                                  '-r', l_ruleset_path,
                                  '-g', l_geoip2city_path,
                                  '-s', l_geoip2ISP_path])
    time.sleep(1)
    # ------------------------------------------------------
    # yield...
    # ------------------------------------------------------
    yield setup_waflz_server
    # ------------------------------------------------------
    # tear down
    # ------------------------------------------------------
    l_code, l_out, l_err = run_command('kill -9 %d'%(l_subproc.pid))
    time.sleep(0.5)
# ------------------------------------------------------------------------------
# test_bb_modsecurity_ec_access_settings_ignore_args
# ------------------------------------------------------------------------------
def test_bb_modsec_ec_acl_01_block_not_in_ignore_args(setup_waflz_server):
    #"ignore_query_args": ["ignore", "this", "crap"]
    l_uri = G_TEST_HOST + '?' + 'arg1&arg2&arg3&arg4&arg5'
    l_headers = {"host": "myhost.com"}
    l_r = requests.get(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    assert len(l_r_json) > 0
    # print(json.dumps(l_r_json,indent=4))
    assert l_r_json['rule_intercept_status'] == 403
    #assert 'modsecurity_crs_23_request_limits.conf' in l_r_json['sub_event'][0]['rule_file']
    # ensure 403 because exceeded max_num_args
    assert 'Too many arguments in' in l_r_json['rule_msg']
# ------------------------------------------------------------------------------
# test_bb_acl_02_bypass_in_ignore_args
# ------------------------------------------------------------------------------
def test_bb_acl_02_bypass_in_ignore_args(setup_waflz_server):
    #Test that passing ignore args lets it bypass
    #Max arg limit it 4, we pass 7
    l_uri = G_TEST_HOST + '?' + 'arg1&arg2&arg3&arg4&ignore&this&crap'
    l_headers = {"host": "myhost.com"}
    l_r = requests.get(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    assert 'status' in l_r_json
    assert l_r_json['status'] == 'ok'
# ------------------------------------------------------------------------------
# test_bb_acl_03_block_headers_not_in_ignore_header_list
# ------------------------------------------------------------------------------
def test_bb_acl_03_block_headers_not_in_ignore_header_list(setup_waflz_server):
    #ignore_header": ["(?i)(benign-header)", "super-whatever-header", "^D.*"]
    l_uri = G_TEST_HOST
    l_headers = {"host": "myhost.com",
                 "kooky-Header" : "function () { doing this is kinda dumb"
                }
    l_r = requests.get(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    # print(l_r_json)
    # We got an event
    assert len(l_r_json) > 0
    # detect a bash shellshock
    assert 'Bash shellshock attack detected' in l_r_json['sub_event'][0]['rule_msg']
    assert 'REQUEST_HEADERS' in l_r_json['sub_event'][0]['matched_var']['name']
    assert 'ZnVuY3Rpb24gKCkgeyBkb2luZyB0aGlzIGlzIGtpbmRhIGR1bWI=' in l_r_json['sub_event'][0]['matched_var']['value']
# ------------------------------------------------------------------------------
# test_bb_acl_04_bypass_headers_in_ignore_header_list
# ------------------------------------------------------------------------------
def test_bb_acl_04_bypass_headers_in_ignore_header_list(setup_waflz_server):
    #Test ignore headers are ignored
    l_uri = G_TEST_HOST
    l_headers = {"host": "myhost.com",
                 "Benign-Header" : "function () { doing this is kinda dumb",
                 "super-whatever-header" : "function () { doing this is kinda dumb"
                }
    l_r = requests.get(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    assert 'status' in l_r_json
    assert l_r_json['status'] == 'ok'
# -------------------------------------------------------------------------------
# test_bb_acl_05_bypass_headers_in_ignore_header_list_regex
# -------------------------------------------------------------------------------
def test_bb_acl_05_bypass_headers_in_ignore_header_list_regex(setup_waflz_server):
    ########################################
    # Test regex "^D.*"
    ########################################
    l_uri = G_TEST_HOST
    #anything that starts with D should be ignored
    l_headers = {"host": "myhost.com",
                 "Doopdoop" : "function () { doing this is kinda dumb",
                 "Duper-duper-deader" : "function () { doing this is kinda dumb"
                }
    l_r = requests.get(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    assert 'status' in l_r_json
    assert l_r_json['status'] == 'ok'
# ------------------------------------------------------------------------------
# test_bb_acl_06_block_cookie_not_in_ignore_cookie_list
# ------------------------------------------------------------------------------
def test_bb_acl_06_block_cookie_not_in_ignore_cookie_list(setup_waflz_server):
    #"ignore_cookie": ["(?i)(sketchy_origin)", "(?i)(yousocrazy)"]
    l_uri = G_TEST_HOST
    l_headers = {"host": "myhost.com",
                 "Cookie": "blahblah=function () { asdf asdf asdf"
                }
    l_r = requests.get(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    assert len(l_r_json) > 0
    # detect a bash shellshock
    assert 'Bash shellshock attack detected' in l_r_json['sub_event'][0]['rule_msg']
    assert 'REQUEST_HEADERS' in l_r_json['sub_event'][0]['matched_var']['name']
# ------------------------------------------------------------------------------
# test_bb_acl_07_bypass_cookie_in_ignore_cookie_list
# ------------------------------------------------------------------------------
def test_bb_acl_07_bypass_cookie_in_ignore_cookie_list(setup_waflz_server):
    #"ignore_cookie": ["(?i)(sketchy_origin)", "(?i)(yousocrazy)"]
    l_uri = G_TEST_HOST
    l_headers = {"host" : "myhost.com",
                 "Cookie" : "SkeTchy_Origin=function () { asdf asdf asdf"
                }
    l_r = requests.get(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    #We get no event
    assert 'status' in l_r_json
    assert l_r_json['status'] == 'ok'
    l_uri = G_TEST_HOST
    l_headers = {"host" : "myhost.com",
                 "Cookie" : "SkeTchy_Origin=function () { asdf asdf asdf"
                }
    l_r = requests.get(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    assert 'status' in l_r_json
    assert l_r_json['status'] == 'ok'
# ------------------------------------------------------------------------------
# test_bb_acl_08_ignore_cookie_in_ignore_cookie_list
# ------------------------------------------------------------------------------
def test_bb_acl_08_bypass_cookie_in_ignore_cookie_list_regex(setup_waflz_server):
    ########################################
    # Test regex "^[0-9_].*$"
    ########################################
    l_uri = G_TEST_HOST
    l_headers = {"host" : "myhost.com",
                 "Cookie" : "0_123_ADB__bloop=function () { asdf asdf asdf"
                }
    l_r = requests.get(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    assert 'status' in l_r_json
    assert l_r_json['status'] == 'ok'
# ------------------------------------------------------------------------------
# test_bb_acl_09_block_disallowed_http_method
# ------------------------------------------------------------------------------
def test_bb_acl_09_block_disallowed_http_method(setup_waflz_server):
    l_uri = G_TEST_HOST
    l_headers = {"host" : "myhost.com"
                }
    l_r = requests.put(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    assert len(l_r_json) > 0
    assert 'Method is not allowed by policy' in l_r_json['rule_msg']
# ------------------------------------------------------------------------------
# test_bb_acl_10_bypass_empty_allowed_settings
# ------------------------------------------------------------------------------
def test_bb_acl_10_bypass_empty_allowed_settings(setup_waflz_server):
    l_uri = G_TEST_HOST
    # ------------------------------------------------------
    # update template
    # ------------------------------------------------------
    l_file_path = os.path.dirname(os.path.abspath(__file__))
    l_conf = {}
    l_conf_path = os.path.realpath(os.path.join(l_file_path, 'test_bb_acl.waf.prof.json'))
    try:
        with open(l_conf_path) as l_f:
            l_conf = json.load(l_f)
    except Exception as l_e:
        print('error opening config file: %s.  Reason: %s error: %s, doc: %s' % (
            l_conf_path, type(l_e), l_e, l_e.__doc__))
        assert False
    l_conf['general_settings']['allowed_request_content_types'] = []
    l_conf['general_settings']['allowed_http_methods'] = []
    l_url = '%supdate_profile'%(G_TEST_HOST)
    # ------------------------------------------------------
    # urlopen (POST)
    # ------------------------------------------------------
    # print(l_url)
    l_headers = {"Content-Type": "application/json"}
    l_r = requests.post(l_url,
                        headers=l_headers,
                        data=json.dumps(l_conf))
    # ------------------------------------------------------
    # test empty method and content type list is allowed
    # ------------------------------------------------------
    assert l_r.status_code == 200
    # ------------------------------------------------------
    # test method and content is bypassed
    # ------------------------------------------------------
    l_headers = {"host": "myhost.com",
                 "Content-Type" : "select * from banana"
                }
    l_r = requests.put(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    assert 'status' in l_r_json
    assert l_r_json['status'] == 'ok'
# ------------------------------------------------------------------------------
# test_bb_acl_11_ignore_cookie_in_ignore_cookie_list
# ------------------------------------------------------------------------------
def test_bb_acl_11_geoip2_lookup_softfail(setup_waflz_server):
    l_uri = G_TEST_HOST
    l_headers = {"host" : "myhost.com",
                 # use malformed ip for lookups
                 "x-waflz-ip" : "0_123_ADB__bloop"
                }
    l_r = requests.get(l_uri, headers=l_headers)
    assert l_r.status_code == 200
    l_r_json = l_r.json()
    print(l_r_json)
    assert 'status' in l_r_json
    assert l_r_json['status'] == 'ok'
