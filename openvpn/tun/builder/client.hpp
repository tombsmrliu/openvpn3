#ifndef OPENVPN_TUN_BUILDER_CLIENT_H
#define OPENVPN_TUN_BUILDER_CLIENT_H

#include <openvpn/tun/tununixbase.hpp>
#include <openvpn/tun/builder/base.hpp>
#include <openvpn/addr/addrpair.hpp>
#include <openvpn/options/rgopt.hpp>

namespace openvpn {

  // Generic tun interface that drives a TunBuilderBase API.
  // Used on Android where the TunBuilderBase API is implemented by
  // VpnService.Builder.
  namespace TunBuilderClient {

    // struct used to pass received tun packets
    struct PacketFrom
    {
      typedef ScopedPtr<PacketFrom> SPtr;
      BufferAllocated buf;
    };

    OPENVPN_EXCEPTION(tun_builder_error);
    OPENVPN_EXCEPTION(tun_builder_route_error);
    OPENVPN_EXCEPTION(tun_builder_dhcp_option_error);

    // A simplified tun interface where pre-existing
    // socket is provided.
    template <typename ReadHandler>
    class Tun : public TunUnixBase<ReadHandler, PacketFrom>
    {
      typedef TunUnixBase<ReadHandler, PacketFrom> Base;

    public:
      typedef boost::intrusive_ptr<Tun> Ptr;

      Tun(boost::asio::io_service& io_service,
	  const int socket,
	  ReadHandler read_handler_arg,
	  const Frame::Ptr& frame_arg,
	  const SessionStats::Ptr& stats_arg)
	: Base(read_handler_arg, frame_arg, stats_arg)
      {
	Base::sd = new boost::asio::posix::stream_descriptor(io_service, socket);
	Base::name_ = "tun";
      }

      ~Tun() { Base::stop(); }
    };

    // A factory for the Client class
    class ClientConfig : public TunClientFactory
    {
    public:
      typedef boost::intrusive_ptr<ClientConfig> Ptr;

      std::string session_name;
      int mtu;                   // optional
      int n_parallel;            // number of parallel async reads on tun socket
      Frame::Ptr frame;
      SessionStats::Ptr stats;

      TunBuilderBase* builder;

      static Ptr new_obj()
      {
	return new ClientConfig;
      }

      virtual TunClient::Ptr new_client_obj(boost::asio::io_service& io_service,
					    TunClientParent& parent);
    private:
      ClientConfig()
	: mtu(0), n_parallel(8), builder(NULL) {}
    };

    // The tun interface
    class Client : public TunClient
    {
      friend class ClientConfig;  // calls constructor
      friend class TunUnixBase<Client*, PacketFrom>;  // calls tun_read_handler

      typedef Tun<Client*> TunImpl;

    public:
      virtual void client_start(const OptionList& opt, TransportClient& transcli)
      {
	if (!impl)
	  {
	    halt = false;
	    try {
	      TunBuilderBase* tb = config->builder;

	      // reset target tun builder object
	      if (!tb->tun_builder_new())
		throw tun_builder_error("tun_builder_new failed");

	      // do ifconfig
	      parent.tun_pre_tun_config();
	      tun_ifconfig(opt, tb);

	      // add routes
	      add_routes(opt, tb);

	      // add DNS servers and domain prefixes
	      add_dns(opt, tb);

	      // set MTU
	      if (config->mtu)
		{
		  if (!tb->tun_builder_set_mtu(config->mtu))
		    throw tun_builder_error("tun_builder_set_mtu failed");
		}

	      // set session name
	      if (!config->session_name.empty())
		{
		  if (!tb->tun_builder_set_session_name(config->session_name))
		    throw tun_builder_error("tun_builder_set_session_name failed");
		}

	      // start tun
	      const int sd = tb->tun_builder_establish();
	      if (sd == -1)
		throw tun_builder_error("cannot acquire tun interface socket");
	      impl.reset(new TunImpl(io_service,
				     sd,
				     this,
				     config->frame,
				     config->stats
				     ));
	      impl->start(config->n_parallel);

	      // signal that we are connected
	      parent.tun_connected();
	    }
	    catch (const std::exception& e)
	      {
		config->stats->error(Error::TUN_SETUP_FAILED);
		stop();
		parent.tun_error(e);
	      }
	  }
      }

      virtual bool tun_send(BufferAllocated& buf)
      {
	return send(buf);
      }

      virtual std::string tun_name() const
      {
	if (impl)
	  return impl->name();
	else
	  return "UNDEF_TUN";
      }

      virtual std::string vpn_ip() const
      {
	return vpn_ip_addr.to_string();
      }

      virtual void stop() { stop_(); }
      virtual ~Client() { stop_(); }

    private:
      Client(boost::asio::io_service& io_service_arg,
	     ClientConfig* config_arg,
	     TunClientParent& parent_arg)
	:  io_service(io_service_arg),
	   config(config_arg),
	   parent(parent_arg),
	   halt(false)
      {
      }

      bool send(const Buffer& buf)
      {
	if (impl)
	  return impl->write(buf);
	else
	  return false;
      }

      void tun_read_handler(PacketFrom::SPtr& pfp) // called by TunImpl
      {
	parent.tun_recv(pfp->buf);
      }

      void stop_()
      {
	if (!halt)
	  {
	    halt = true;

	    // stop tun
	    if (impl)
	      impl->stop();
	  }
      }

      void tun_ifconfig(const OptionList& opt, TunBuilderBase* tb)
      {
	// first verify topology
	{
	  const Option& o = opt.get("topology");
	  o.min_args(2);
	  if (o[1] != "subnet")
	    throw option_error("only topology subnet supported");
	}

	// configure tun interface
	{
	  const Option& o = opt.get("ifconfig");
	  o.min_args(2);
	  const IP::AddrMaskPair pair = IP::AddrMaskPair::from_string(o[1], o.get_empty(2), "ifconfig");
	  if (!tb->tun_builder_add_address(pair.addr.to_string(), pair.netmask.prefix_len()))
	    throw tun_builder_error("tun_builder_add_address failed");
	  vpn_ip_addr = pair.addr;
	}
      }

      void add_routes(const OptionList& opt, TunBuilderBase* tb)
      {
	// do redirect-gateway
	const RedirectGatewayFlags rg_flags(opt);
	if (rg_flags.redirect_gateway_enabled()) // fixme -- support IPv6
	  {
	    if (!tb->tun_builder_add_route("0.0.0.0", 0)) 
	      throw tun_builder_route_error("tun_builder_add_route for redirect-gateway failed");
	  }
	else
	  {
	    OptionList::IndexMap::const_iterator dopt = opt.map().find("route");
	    if (dopt != opt.map().end())
	      {
		for (OptionList::IndexList::const_iterator i = dopt->second.begin(); i != dopt->second.end(); i++)
		  {
		    const Option& o = opt[*i];
		    try {
		      o.min_args(2);
		      if (o.size() >= 4 && o[3] != "vpn_gateway")
			throw tun_builder_route_error("only tunnel routes supported");
		      const IP::AddrMaskPair pair = IP::AddrMaskPair::from_string(o[1], o.get_empty(2), "route");
		      if (!tb->tun_builder_add_route(pair.addr.to_string(), pair.netmask.prefix_len()))
			throw tun_builder_route_error("tun_builder_add_route failed");
		    }
		    catch (const std::exception& e)
		      {
			OPENVPN_THROW(tun_builder_error, "error parsing received route: " << o.render() << " : " << e.what());
		      }
		  }
	      }
	  }
      }

      void add_dns(const OptionList& opt, TunBuilderBase* tb)
      {
	// Example:
	//   [dhcp-option] [DNS] [172.16.0.23] 
	//   [dhcp-option] [DOMAIN] [openvpn.net] 
	//   [dhcp-option] [DOMAIN] [example.com] 
	OptionList::IndexMap::const_iterator dopt = opt.map().find("dhcp-option");
	if (dopt != opt.map().end())
	  {
	    for (OptionList::IndexList::const_iterator i = dopt->second.begin(); i != dopt->second.end(); i++)
	      {
		const Option& o = opt[*i];
		try {
		  const std::string& type = o.get(1);
		  if (type == "DNS")
		    {
		      o.exact_args(3);
		      const IP::Addr ip = IP::Addr::from_string(o[2], "dns-server-ip");
		      if (!tb->tun_builder_add_dns_server(ip.to_string()))
			throw tun_builder_dhcp_option_error("tun_builder_add_dns_server failed");
		    }
		  else if (type == "DOMAIN")
		    {
		      o.exact_args(3);
		      if (!tb->tun_builder_add_search_domain(o[2]))
			throw tun_builder_dhcp_option_error("tun_builder_add_search_domain failed");
		    }
		  else
		    throw tun_builder_dhcp_option_error("unknown DHCP option");
		}
		catch (const std::exception& e)
		  {
		    OPENVPN_THROW(tun_builder_error, "error parsing received dhcp-option: " << o.render() << " : " << e.what());
		  }
	      }
	  }
      }

      boost::asio::io_service& io_service;
      ClientConfig::Ptr config;
      TunClientParent& parent;
      TunImpl::Ptr impl;
      bool halt;

      IP::Addr vpn_ip_addr;
    };

    inline TunClient::Ptr ClientConfig::new_client_obj(boost::asio::io_service& io_service,
						       TunClientParent& parent)
    {
      return TunClient::Ptr(new Client(io_service, this, parent));
    }

  }
}

#endif