/**
    @file

    RAII lifetime management of libssh2 SFTP channels.

    @if license

    Copyright (C) 2013  Alexander Lamaison <awl03@doc.ic.ac.uk>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

    In addition, as a special exception, the the copyright holders give you
    permission to combine this program with free software programs or the 
    OpenSSL project's "OpenSSL" library (or with modified versions of it, 
    with unchanged license). You may copy and distribute such a system 
    following the terms of the GNU GPL for this program and the licenses 
    of the other code concerned. The GNU General Public License gives 
    permission to release a modified version without this exception; this 
    exception also makes it possible to release a modified version which 
    carries forward this exception.

    @endif
*/

#ifndef SSH_DETAIL_SFTP_CHANNEL_STATE_HPP
#define SSH_DETAIL_SFTP_CHANNEL_STATE_HPP

#include <ssh/detail/libssh2/sftp.hpp> // init
#include <ssh/detail/session_state.hpp>

#include <boost/noncopyable.hpp>

#include <libssh2_sftp.h> // LIBSSH2_SFTP

namespace ssh {
namespace detail {

inline LIBSSH2_SFTP* do_sftp_init(session_state& session)
{
    session_state::scoped_lock lock = session.aquire_lock();

    return libssh2::sftp::init(session.session_ptr());
}

/**
 * RAII object managing SFTP channel state that must be maintained together.
 *
 * Manages the graceful startup/shutdown the SFTP channel and does so in
 * a thread-safe manner.
 */
class sftp_channel_state : private boost::noncopyable
{
    //
    // Intentionally not movable to prevent the public classes that own
    // this object moving it when they are themselves moved.  This object
    // is referenced by other classes that don't own it so the owning classes
    // need to leave it where it is when they move so as not to invalidate
    // the other references.  Making this non-copyable, non-movable enforces
    // that.
    // 
public:

    typedef session_state::scoped_lock scoped_lock;

    /**
     * Creates SFTP channel that closes itself in a thread-safe manner
     * when it goes out of scope.
     */
    sftp_channel_state(session_state& session)
        : m_session(session), m_sftp(do_sftp_init(session_ref())) {}

    ~sftp_channel_state() throw()
    {
        session_state::scoped_lock lock = session_ref().aquire_lock();

        ::libssh2_sftp_shutdown(m_sftp);
    }

    scoped_lock aquire_lock()
    {
        return session_ref().aquire_lock();
    }

    LIBSSH2_SESSION* session_ptr()
    {
        return session_ref().session_ptr();
    }

    LIBSSH2_SFTP* sftp_ptr()
    {
        return m_sftp;
    }

private:

    session_state& session_ref()
    {
        return m_session;
    }

    session_state& m_session;
    LIBSSH2_SFTP* m_sftp;
};

}} // namespace ssh::detail

#endif
