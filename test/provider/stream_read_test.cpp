/**
    @file

    Unit tests for CSftpStream exercising the read mechanism.

    @if license

    Copyright (C) 2009, 2011, 2013  Alexander Lamaison <awl03@doc.ic.ac.uk>

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

#include "test/provider/StreamFixture.hpp"
#include "test/common_boost/helpers.hpp"
#include "test/common_boost/stream_utils.hpp" // verify_stream_read

#include "swish/atl.hpp"

#include <comet/ptr.h> // com_ptr

#include <boost/test/unit_test.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/numeric/conversion/cast.hpp> // numeric_cast
#include <boost/system/system_error.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/throw_exception.hpp>  // BOOST_THROW_EXCEPTION

#include <string>
#include <vector>

#include <sys/stat.h>  // _S_IREAD

using test::provider::StreamFixture;
using test::stream_utils::verify_stream_read;

using comet::com_ptr;

using boost::filesystem::ofstream;
using boost::numeric_cast;
using boost::system::system_error;
using boost::system::get_system_category;
using boost::shared_ptr;

using std::string;
using std::vector;

namespace { // private

    const string TEST_DATA = "Humpty dumpty\nsat on the wall.\n\rHumpty ...";

    /**
     * Fixture for tests that need to read data from an existing file.
     */
    class StreamReadFixture : public StreamFixture
    {
    public:

        /**
         * Put test data into a file in our sandbox.
         */
        StreamReadFixture() : StreamFixture()
        {
            ofstream file(m_local_path, std::ios::binary);
            file << ExpectedData() << std::flush;
        }

        /**
         * Create an IStream instance open for reading on a temporary file 
         * in our sandbox.  The file contained the same data that 
         * ExpectedData() returns.
         */
        com_ptr<IStream> GetReadStream()
        {
            return GetStream(std::ios_base::in);
        }

        /**
         * Return the data we expect to be able to read using the IStream.
         */
        string ExpectedData()
        {
            return TEST_DATA;
        }
    };

}

BOOST_FIXTURE_TEST_SUITE(StreamRead, StreamReadFixture)

/**
 * Simply get a stream.
 */
BOOST_AUTO_TEST_CASE( get )
{
    com_ptr<IStream> stream = GetReadStream();
    BOOST_REQUIRE(stream);
}

/**
 * Get a read stream to a read-only file.
 * This tests that we aren't inadvertently asking for more permissions than
 * we need.
 */
BOOST_AUTO_TEST_CASE( get_readonly )
{
    if (_wchmod(m_local_path.file_string().c_str(), _S_IREAD) != 0)
        BOOST_THROW_EXCEPTION(system_error(errno, get_system_category()));

    com_ptr<IStream> stream = GetReadStream();
    BOOST_REQUIRE(stream);
}

/**
 * Read a sequence of characters.
 */
BOOST_AUTO_TEST_CASE( read_a_string )
{
    com_ptr<IStream> stream = GetReadStream();

    string expected = ExpectedData();
    vector<char> buf(expected.size());

    size_t bytes_read =    verify_stream_read(
        &buf[0], numeric_cast<ULONG>(buf.size()), stream);

    BOOST_CHECK_EQUAL(bytes_read, expected.size());

    // Test that the bytes we read match
    BOOST_REQUIRE_EQUAL_COLLECTIONS(
        buf.begin(), buf.end(), expected.begin(), expected.end());
}

/**
 * Read a sequence of characters from a read-only file.
 */
BOOST_AUTO_TEST_CASE( read_a_string_readonly )
{
    if (_wchmod(m_local_path.file_string().c_str(), _S_IREAD) != 0)
        BOOST_THROW_EXCEPTION(system_error(errno, get_system_category()));

    com_ptr<IStream> stream = GetReadStream();

    string expected = ExpectedData();
    vector<char> buf(expected.size());

    size_t bytes_read =    verify_stream_read(
        &buf[0], numeric_cast<ULONG>(buf.size()), stream);

    BOOST_CHECK_EQUAL(bytes_read, expected.size());

    // Test that the bytes we read match
    BOOST_REQUIRE_EQUAL_COLLECTIONS(
        buf.begin(), buf.end(), expected.begin(), expected.end());
}

/**
 * Try to read from a locked file.
 * This tests how we deal with a failure in a read case.  In order to
 * force a failure we open the stream but then lock the first 30 bytes
 * of the file that's under it before trying to read from the stream.
 */

BOOST_AUTO_TEST_CASE( read_fail )
{
    com_ptr<IStream> stream = GetReadStream();

    // Open stream's file
    shared_ptr<void> file_handle(
        ::CreateFile(
            m_local_path.file_string().c_str(), 
            GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL),
        ::CloseHandle);
    if (file_handle.get() == INVALID_HANDLE_VALUE)
        throw system_error(::GetLastError(), get_system_category());

    // Lock it
    if (!::LockFile(file_handle.get(), 0, 0, 30, 0))
        throw system_error(::GetLastError(), get_system_category());

    // Try to read from the stream
    try
    {
        string expected = ExpectedData();
        ULONG cbRead = 0;
        vector<char> buf(expected.size());
        BOOST_REQUIRE(FAILED(
            stream->Read(&buf[0], numeric_cast<ULONG>(buf.size()),
            &cbRead)));
        BOOST_REQUIRE_EQUAL(cbRead, 0U);
    }
    catch (...)
    {
        ::UnlockFile(file_handle.get(), 0, 0, 30, 0);
        throw;
    }
}

BOOST_AUTO_TEST_SUITE_END()
