//
//  rgopt.hpp
//  OpenVPN
//
//  Copyright (c) 2012 OpenVPN Technologies, Inc. All rights reserved.
//

// This class handles parsing and representation of redirect-gateway
// and redirect-private directives.

#ifndef OPENVPN_CLIENT_RGOPT_H
#define OPENVPN_CLIENT_RGOPT_H

#include <openvpn/common/options.hpp>

namespace openvpn {
  class RedirectGatewayFlags {
  public:
    enum Flags {
      RG_ENABLE      = (1<<0),
      RG_REROUTE_GW  = (1<<1),
      RG_LOCAL       = (1<<2),
      RG_AUTO_LOCAL  = (1<<3),
      RG_DEF1        = (1<<4),
      RG_BYPASS_DHCP = (1<<5),
      RG_BYPASS_DNS  = (1<<6),
      RG_BLOCK_LOCAL = (1<<7),
      RG_IPv4        = (1<<8),
      RG_IPv6        = (1<<9),

      RG_DEFAULT     = (RG_IPv4),
    };

    RedirectGatewayFlags() : flags_(RG_DEFAULT) {}

    explicit RedirectGatewayFlags(const OptionList& opt)
    {
      init(opt);
    }

    void init(const OptionList& opt)
    {
      flags_ = RG_DEFAULT;
      doinit(opt, "redirect-gateway", true);  // DIRECTIVE
      doinit(opt, "redirect-private", false); // DIRECTIVE
    }

    unsigned int operator()() const { return flags_; }

    bool redirect_gateway_ipv4_enabled() const
    {
      return rg_enabled() && (flags_ & RG_IPv4);
    }

    bool redirect_gateway_ipv6_enabled() const
    {
      return rg_enabled() && (flags_ & RG_IPv6);
    }

  private:
    bool rg_enabled() const
    {
      return (flags_ & (RG_ENABLE|RG_REROUTE_GW)) == (RG_ENABLE|RG_REROUTE_GW);
    }

    void doinit(const OptionList& opt, const std::string& directive, const bool redirect_gateway)
    {
      OptionList::IndexMap::const_iterator rg = opt.map().find(directive);
      if (rg != opt.map().end())
	add_flags(opt, rg->second, redirect_gateway);
    }

    void add_flags(const OptionList& opt, const OptionList::IndexList& idx, const bool redirect_gateway)
    {
      flags_ |= RG_ENABLE;
      if (redirect_gateway)
	flags_ |= RG_REROUTE_GW;
      else
	flags_ &= ~RG_REROUTE_GW;
      for (OptionList::IndexList::const_iterator i = idx.begin(); i != idx.end(); ++i)
	{
	  const Option& o = opt[*i];
	  for (size_t j = 1; j < o.size(); ++j)
	    {
	      const std::string& f = o.get(j, 64);
	      if (f == "local")
		flags_ |= RG_LOCAL;
	      else if (f == "autolocal")
		flags_ |= RG_AUTO_LOCAL;
	      else if (f == "def1")
		flags_ |= RG_DEF1;
	      else if (f == "bypass-dhcp")
		flags_ |= RG_BYPASS_DHCP;
	      else if (f == "bypass-dns")
		flags_ |= RG_BYPASS_DNS;
	      else if (f == "block_local")
		flags_ |= RG_BLOCK_LOCAL;
	      else if (f == "ipv4")
		flags_ |= RG_IPv4;
	      else if (f == "!ipv4")
		flags_ &= ~RG_IPv4;
	      else if (f == "ipv6")
		flags_ |= RG_IPv6;
	      else if (f == "!ipv6")
		flags_ &= ~RG_IPv6;
	    }
	}
    }

    unsigned int flags_;
  };
}

#endif