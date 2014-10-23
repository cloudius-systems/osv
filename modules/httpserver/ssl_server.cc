/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.hh>

#include "transport.hh"
#include "ssl_server.hh"

namespace http {

namespace server {

using ssl_socket = ssl::stream<tcp::socket>;

class ssl_transport : public transport {
private:
    std::shared_ptr<ssl_socket> _socket;
public:
    ssl_transport(std::shared_ptr<ssl_socket> socket)
        : _socket(socket)
    {
    }

    void close(std::function<void(boost::system::error_code)> callback) override
    {
        _socket->async_shutdown([=] (boost::system::error_code ec) {
            if (ec != boost::asio::error::operation_aborted) {
                _socket->lowest_layer().close();
            }
            callback(ec);
        });
    }

    void async_read_some(boost::asio::mutable_buffers_1 buf,
        std::function<void(boost::system::error_code, std::size_t)>&& callback) override
    {
        _socket->async_read_some(buf, std::move(callback));
    }

    void async_write(std::vector<boost::asio::const_buffer> buffers,
        std::function<void(boost::system::error_code, std::size_t)>&& callback) override
    {
        boost::asio::async_write(*_socket, buffers, std::move(callback));
    }

    std::string get_protocol_name()
    {
        return "https";
    }
};

ssl_acceptor::ssl_acceptor(boost::asio::io_service& io_service,
            boost::asio::ssl::context&& ctx,
            tcp::acceptor&& tcp_acceptor)
    : _io_service(io_service)
    , _ctx(std::move(ctx))
    , _tcp_acceptor(std::move(tcp_acceptor))
{
}

void ssl_acceptor::close()
{
    _tcp_acceptor.close();
}

void ssl_acceptor::do_accept(callback_t callback)
{
    auto socket = std::make_shared<ssl_socket>(_io_service, _ctx);

    _tcp_acceptor.async_accept(socket->lowest_layer(),
        [this, socket, callback] (boost::system::error_code ec) {
            if (!_tcp_acceptor.is_open()) {
                return;
            }

            if (!ec) {
                socket->async_handshake(ssl::stream_base::server,
                    [this, socket, callback] (boost::system::error_code ec) {
                        if (ec) {
                            auto remote = socket->lowest_layer().remote_endpoint();
                            debug("handshake with " + remote.address().to_string()
                                + " failed: " + ec.message() + "\n");
                        }

                        if (!_tcp_acceptor.is_open()) {
                            return;
                        }

                        if (!ec) {
                            callback(std::make_shared<ssl_transport>(socket));
                        }
                    });
            }

            do_accept(callback);
        });
}

static void set_client_CA_list(ssl::context& ctx, const std::string& cert_path)
{
    STACK_OF(X509_NAME) *cert_names;
    cert_names = ::SSL_load_client_CA_file(cert_path.c_str());
    if (cert_names == NULL) {
        throw std::runtime_error("No CA names found in " + cert_path);
    }
    ::SSL_CTX_set_client_CA_list(ctx.native_handle(), cert_names);
}

ssl::context make_ssl_context(const std::string& ca_cert_path,
    const std::string& cert_path, const std::string& key_path)
{
    ssl::context ctx(ssl::context::tlsv12);

    ctx.set_verify_mode(ssl::verify_peer | ssl::verify_fail_if_no_peer_cert);
    ctx.load_verify_file(ca_cert_path);

    // We must set the client CA name list, otherwise browsers will error out
    // on connection attempt.
    set_client_CA_list(ctx, ca_cert_path);

    ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2);
    ctx.use_certificate_file(cert_path, ssl::context::pem);
    ctx.use_private_key_file(key_path, ssl::context::pem);

    unsigned char session_ctx_id[32];
    if (!RAND_bytes(session_ctx_id, sizeof(session_ctx_id))) {
        throw std::runtime_error("RAND_bytes() failed, err=" + std::to_string(ERR_get_error()));
    }
    ::SSL_CTX_set_session_id_context(ctx.native_handle(), session_ctx_id, sizeof(session_ctx_id));

    return ctx;
}

}

}
