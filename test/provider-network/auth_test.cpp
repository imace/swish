/**
    @file

    Testing session authentication over a real network connection.

    @if license

    Copyright (C) 2008, 2009, 2010, 2012, 2013
    Alexander Lamaison <awl03@doc.ic.ac.uk>

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

    @endif
*/

#include "test/common_boost/helpers.hpp"
#include "test/common_boost/MockConsumer.hpp"
#include "test/common_boost/remote_test_config.hpp"

#include "swish/connection/authenticated_session.hpp"

#include <comet/error.h> // com_error
#include <comet/ptr.h> // com_ptr

#include <boost/system/system_error.hpp>
#include <boost/test/unit_test.hpp>

#include <exception>

using test::MockConsumer;
using test::remote_test_config;

using swish::connection::authenticated_session;

using comet::com_error;
using comet::com_ptr;

using boost::system::system_error;
using boost::test_tools::predicate_result;

using std::exception;

namespace {

    /**
     * Check that the given session responds sensibly to a request.
     */
    predicate_result alive(authenticated_session& session)
    {
        try
        {
            session.get_sftp_filesystem().directory_iterator("/");

            predicate_result res(true);
            res.message() << "Session seems to be alive";
            return res;
        }
        catch(const exception& e)
        {
            predicate_result res(false);
            res.message() << "Session seems to be dead: " << e.what();
            return res;
        }
    }

    bool is_e_abort(com_error e)
    {
        return e.hr() == E_ABORT;
    }
}

// These tests use the host defined in the TEST_HOST_NAME, TEST_HOST_PORT,
// TEST_USER_NAME and TEST_PASSWORD environment variables.  This is necessary
// because our usual local OpenSSH server setup used for all the other tests
// can't test passwords as OpenSSH will always use a Windows user account
// and we can't get at those passwords.  

BOOST_AUTO_TEST_SUITE( network_auth_tests )

BOOST_AUTO_TEST_CASE( SimplePasswordAuthentication )
{
    // Choose mock behaviours to force only simple password authentication
    com_ptr<MockConsumer> consumer = new MockConsumer();
    consumer->set_password_behaviour(MockConsumer::CustomPassword);
    consumer->set_keyboard_interactive_behaviour(MockConsumer::AbortResponse);
    consumer->set_pubkey_behaviour(MockConsumer::AbortKeys);

    remote_test_config config;
    consumer->set_password(config.GetPassword());

    // Fails if keyboard-int supported on the server as that gets preference
    // and replies with user-aborted
    authenticated_session session =
        config.as_connection_spec().create_session(consumer);

    BOOST_CHECK(alive(session));
}

BOOST_AUTO_TEST_CASE( KeyboardInteractiveAuthentication )
{
    // Choose mock behaviours to force only kbd-interactive authentication
    com_ptr<MockConsumer> consumer = new MockConsumer();
    consumer->set_password_behaviour(MockConsumer::FailPassword);
    consumer->set_pubkey_behaviour(MockConsumer::AbortKeys);
    consumer->set_keyboard_interactive_behaviour(MockConsumer::CustomResponse);

    remote_test_config config;
    consumer->set_password(config.GetPassword());

    // This may fail if the server (which we can't control) doesn't allow
    // ki-auth
    authenticated_session session =
        config.as_connection_spec().create_session(consumer);

    BOOST_CHECK(alive(session));
}

BOOST_AUTO_TEST_CASE( WrongPasswordOrResponse )
{
    com_ptr<MockConsumer> consumer = new MockConsumer();

    consumer->set_pubkey_behaviour(MockConsumer::AbortKeys);
    // We don't know which of password or kb-int (or both) is set up on the
    // server so we have to prime both to return the wrong password else
    // we may get E_ABORT for the kb-interactive response
    consumer->set_keyboard_interactive_behaviour(MockConsumer::WrongResponse);
    consumer->set_password_behaviour(MockConsumer::WrongPassword);

    remote_test_config config;
    // FIXME: Any exception will do.  We don't have fine enough control over the
    // mock to test this properly.
    BOOST_CHECK_THROW(
        config.as_connection_spec().create_session(consumer), exception);
}

BOOST_AUTO_TEST_CASE( UserAborted )
{
    com_ptr<MockConsumer> consumer = new MockConsumer();

    consumer->set_keyboard_interactive_behaviour(MockConsumer::AbortResponse);
    consumer->set_password_behaviour(MockConsumer::AbortPassword);
    consumer->set_pubkey_behaviour(MockConsumer::AbortKeys);

    remote_test_config config;
    BOOST_CHECK_EXCEPTION(
        config.as_connection_spec().create_session(consumer), com_error,
        is_e_abort);
}

/**
 * Test to see that we can connect successfully after an aborted attempt.
 */
BOOST_AUTO_TEST_CASE( ReconnectAfterAbort )
{
    // Choose mock behaviours to simulate a user cancelling authentication
    com_ptr<MockConsumer> consumer = new MockConsumer();
    consumer->set_pubkey_behaviour(MockConsumer::AbortKeys);
    consumer->set_password_behaviour(MockConsumer::AbortPassword);
    consumer->set_keyboard_interactive_behaviour(
        MockConsumer::AbortResponse);

    remote_test_config config;
    BOOST_CHECK_EXCEPTION(
        config.as_connection_spec().create_session(consumer), com_error,
        is_e_abort);

    // Change mock behaviours so that authentication succeeds
    consumer->set_password_max_attempts(2);
    consumer->set_keyboard_interactive_max_attempts(2);
    consumer->set_password_behaviour(MockConsumer::CustomPassword);
    consumer->set_keyboard_interactive_behaviour(MockConsumer::CustomResponse);

    consumer->set_password(config.GetPassword());

    authenticated_session session =
        config.as_connection_spec().create_session(consumer);

    BOOST_CHECK(alive(session));
}

BOOST_AUTO_TEST_SUITE_END();