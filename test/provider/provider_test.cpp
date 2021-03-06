/**
    @file

    Tests for CProvider.

    @if license

    Copyright (C) 2010, 2012, 2013  Alexander Lamaison <awl03@doc.ic.ac.uk>

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

#include "swish/provider/sftp_provider.hpp" // sftp_provider, listing

#include "test/common_boost/helpers.hpp"
#include "test/common_boost/ProviderFixture.hpp" // ProviderFixture

#include <comet/bstr.h> // bstr_t
#include <comet/datetime.h> // datetime_t, timeperiod_t
#include <comet/error.h> // com_error
#include <comet/ptr.h> // com_ptr

#include <boost/filesystem/path.hpp> // wpath
#include <boost/filesystem/fstream.hpp> // ofstream taking a path
#include <boost/foreach.hpp> // BOOST_FOREACH
#include <boost/range/empty.hpp>
#include <boost/range/size.hpp>

#include <boost/test/unit_test.hpp>

#include <exception>
#include <string>
#include <vector>

using test::MockConsumer;
using test::ProviderFixture;

using swish::provider::sftp_filesystem_item;
using swish::provider::sftp_provider;
using swish::provider::directory_listing;

using comet::bstr_t;
using comet::com_error;
using comet::com_error_from_interface;
using comet::com_ptr;
using comet::datetime_t;
using comet::timeperiod_t;

using boost::empty;
using boost::filesystem::ofstream;
using boost::filesystem::wpath;
using boost::shared_ptr;
using boost::test_tools::predicate_result;

using std::exception;
using std::string;
using std::vector;
using std::wstring;


namespace {

    const string longentry = 
        "-rw-r--r--    1 swish    wheel         767 Dec  8  2005 .cshrc";

    predicate_result file_exists_in_listing(
        const wstring& filename, const directory_listing& listing)
    {
        if (empty(listing))
        {
            predicate_result res(false);
            res.message() << "Enumerator is empty";
            return res;
        }
        else
        {
            BOOST_FOREACH(const sftp_filesystem_item& entry, listing)
            {
                if (filename == entry.filename())
                {
                    predicate_result res(true);
                    res.message() << "File found in enumerator: " << filename;
                    return res;
                }
            }

            predicate_result res(false);
            res.message() << "File not in enumerator: " << filename;
            return res;
        }
    }
}



BOOST_FIXTURE_TEST_SUITE( provider_tests, ProviderFixture )

BOOST_AUTO_TEST_SUITE( listing_tests )

/**
 * List contents of an empty directory.
 */
BOOST_AUTO_TEST_CASE( list_empty_dir )
{
    shared_ptr<sftp_provider> provider = Provider();
    BOOST_CHECK_EQUAL(
        boost::size(provider->listing(ToRemotePath(Sandbox()))), 0U);
}

/**
 * List contents of a directory.
 */
BOOST_AUTO_TEST_CASE( list_dir )
{
    wpath file1 = NewFileInSandbox();
    wpath file2 = NewFileInSandbox();

    directory_listing listing =
        Provider()->listing(ToRemotePath(Sandbox()));

    BOOST_CHECK_EQUAL(boost::size(listing), 2U);

    BOOST_CHECK_EQUAL(listing[0].filename(), file1.filename());
    BOOST_CHECK_EQUAL(listing[1].filename(), file2.filename());

    // Check format of listing is sensible
    BOOST_FOREACH(const sftp_filesystem_item& entry, listing)
    {
        wstring filename = entry.filename().string();

        BOOST_CHECK(!filename.empty());
        BOOST_CHECK_NE(filename, L".");
        BOOST_CHECK_NE(filename, L"..");

        BOOST_CHECK(!entry.owner()->empty());
        BOOST_CHECK(!entry.group()->empty());

        // We don't know the exact date but check that it's very recent
        BOOST_CHECK(entry.last_modified().valid());
        BOOST_CHECK_GT(
            entry.last_modified(),
            datetime_t::now() - timeperiod_t(0, 0, 0, 10)); // max 10 secs ago

        BOOST_CHECK(entry.last_accessed().valid());
        BOOST_CHECK_GT(
            entry.last_accessed(),
            datetime_t::now() - timeperiod_t(0, 0, 0, 10)); // max 10 secs ago
    }
}

BOOST_AUTO_TEST_CASE( list_dir_many )
{
    // Fetch 5 listing enumerators
    vector<directory_listing> enums(5);

    BOOST_FOREACH(directory_listing& listing, enums)
    {
        listing = Provider()->listing(ToRemotePath(Sandbox()));
    }
}

BOOST_AUTO_TEST_CASE( listing_independence )
{
    // Put some files in the test area

    wpath file1 = NewFileInSandbox();
    wpath file2 = NewFileInSandbox();
    wpath file3 = NewFileInSandbox();

    // Fetch first listing enumerator
    directory_listing listing_before =
        Provider()->listing(ToRemotePath(Sandbox()));

    // Delete one of the files
    remove(file2);

    // Fetch second listing enumerator
    directory_listing listing_after = 
        Provider()->listing(ToRemotePath(Sandbox()));

    // The first listing should still show the file. The second should not.
    BOOST_CHECK(
        file_exists_in_listing(file1.filename(), listing_before));
    BOOST_CHECK(
        file_exists_in_listing(file2.filename(), listing_before));
    BOOST_CHECK(
        file_exists_in_listing(file3.filename(), listing_before));
    BOOST_CHECK(
        file_exists_in_listing(file1.filename(), listing_after));
    BOOST_CHECK(
        !file_exists_in_listing(file2.filename(), listing_after));
    BOOST_CHECK(
        file_exists_in_listing(file3.filename(), listing_after));
}

namespace {

    predicate_result is_failed_to_open(const exception& e)
    {
        string expected = "Failed opening remote file: FX_NO_SUCH_FILE";
        string actual = e.what();

        if (expected != actual)
        {
            predicate_result res(false);
            res.message()
                << "Exception is not failure to open [" << expected
                << " != " << actual << "]";
            return res;
        }

        return true;
    }

}

/**
 * Try to list non-existent directory.
 */
BOOST_AUTO_TEST_CASE( list_dir_error )
{
    shared_ptr<sftp_provider> provider = Provider();

    BOOST_CHECK_EXCEPTION(
        provider->listing(L"/i/dont/exist"), exception, is_failed_to_open);
}

/**
 * Can we handle a unicode filename?
 */
BOOST_AUTO_TEST_CASE( unicode )
{
    // create an empty file with a unicode filename in the sandbox
    wpath unicode_file_name = NewFileInSandbox(L"русский");
    BOOST_CHECK(unicode_file_name.is_complete());

    directory_listing listing =
        Provider()->listing(ToRemotePath(Sandbox()));

    BOOST_CHECK_EQUAL(
        listing[0].filename(), unicode_file_name.filename());
}

/**
 * Can we see inside directories whose names are non-latin Unicode?
 */
BOOST_AUTO_TEST_CASE( list_unicode_dir )
{
    wpath directory = NewDirectoryInSandbox(L"漢字 العربية русский 47");
    wpath file = directory / L"latin filename";
    ofstream(file).close();

    Provider()->listing(ToRemotePath(directory));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( renaming_tests )

BOOST_AUTO_TEST_CASE( rename_file )
{
    wpath file = NewFileInSandbox();
    wpath renamed_file = file.parent_path() / (file.filename() + L"renamed");

    shared_ptr<sftp_provider> provider = Provider();

    bstr_t old_name(ToRemotePath(file).string());
    bstr_t new_name(ToRemotePath(renamed_file).string());

    BOOST_CHECK_EQUAL(
        provider->rename(Consumer().get(), old_name.in(), new_name.in()),
        VARIANT_FALSE);
    BOOST_CHECK(exists(renamed_file));
    BOOST_CHECK(!exists(file));

    // Rename back
    BOOST_CHECK_EQUAL(
        provider->rename(Consumer().get(), new_name.in(), old_name.in()),
        VARIANT_FALSE);
    BOOST_CHECK(!exists(renamed_file));
    BOOST_CHECK(exists(file));
}

BOOST_AUTO_TEST_CASE( rename_unicode_file )
{
    // create an empty file with a unicode filename in the sandbox
    wpath unicode_file_name = NewFileInSandbox(L"русский.txt");

    wpath renamed_file = Sandbox() / L"Россия";

    shared_ptr<sftp_provider> provider = Provider();

    bstr_t old_name(ToRemotePath(unicode_file_name).string());
    bstr_t new_name(ToRemotePath(renamed_file).string());

    BOOST_CHECK_EQUAL(
        provider->rename(Consumer().get(), old_name.in(), new_name.in()),
        VARIANT_FALSE);

    BOOST_CHECK(exists(renamed_file));
    BOOST_CHECK(!exists(unicode_file_name));
}

/**
 * Test that we prompt the user to confirm overwrite and that we
 * perform the overwrite correctly because the user approved to operation.
 */
BOOST_AUTO_TEST_CASE( rename_with_obstruction )
{
    com_ptr<MockConsumer> consumer = Consumer();
    consumer->set_confirm_overwrite_behaviour(MockConsumer::AllowOverwrite);

    wpath subject = NewFileInSandbox();

    // Obstruct renaming by creating an empty file on the target location
    wpath target = NewFileInSandbox(subject.filename() + L"renamed");

    // Swish creates a temporary for non-atomic overwrite to minimise the
    // chance of failing to rename but losing the overwritten file as well.
    // We need to check this gets removed correctly.
    wpath swish_rename_temp_file =
        target.parent_path() / (target.filename() + L".swish_rename_temp");

    // Check that the non-atomic overwrite temp does not already exists
    BOOST_CHECK(!exists(swish_rename_temp_file));

    BOOST_CHECK_EQUAL(
        Provider()->rename(
            consumer.in(), bstr_t(subject.string()).in(),
            bstr_t(target.string()).in()),
        VARIANT_TRUE);

    // The consumer should have been prompted for permission
    BOOST_CHECK(consumer->was_asked_to_confirm_overwrite());

    // Check that the old file no longer exists but the target does
    BOOST_CHECK(!exists(subject));
    BOOST_CHECK(exists(target));

    // Check that the non-atomic overwrite temp has been removed
    BOOST_CHECK(!exists(swish_rename_temp_file));
}

namespace {

    bool is_abort(const com_error& error)
    {
        return error.hr() == E_ABORT;
    }
}

/**
 * Test that we prompt the user to confirm overwrite and that we
 * do not perform the overwrite because the user denied permission.
 *
 * TODO: check the contents of the target file to make sure it is untouched.
 */
BOOST_AUTO_TEST_CASE( rename_with_obstruction_refused_overwrite_permission )
{
    com_ptr<MockConsumer> consumer = Consumer();
    consumer->set_confirm_overwrite_behaviour(MockConsumer::PreventOverwrite);

    wpath subject = NewFileInSandbox();
    // Obstruct renaming by creating an empty file on the target location
    wpath target = NewFileInSandbox(subject.filename() + L"renamed");

    BOOST_CHECK_EXCEPTION(
        Provider()->rename(
            consumer.in(), bstr_t(subject.string()).in(),
            bstr_t(target.string()).in()),
        com_error, is_abort);

    // The consumer should have been prompted for permission
    BOOST_CHECK(consumer->was_asked_to_confirm_overwrite());

    // Check that both files still exist
    BOOST_CHECK(exists(subject));
    BOOST_CHECK(exists(target));
}

/*
 * The next three tests just duplicate the ones above but for directories
 * instead of files.
 */

BOOST_AUTO_TEST_CASE( rename_directory )
{
    wpath subject = NewDirectoryInSandbox();
    wpath target = subject.parent_path() / (subject.filename() + L"renamed");

    shared_ptr<sftp_provider> provider = Provider();

    bstr_t old_name(ToRemotePath(subject).string());
    bstr_t new_name(ToRemotePath(target).string());

    BOOST_CHECK_EQUAL(
        provider->rename(Consumer().get(), old_name.in(), new_name.in()),
        VARIANT_FALSE);
    BOOST_CHECK(exists(target));
    BOOST_CHECK(is_directory(target));
    BOOST_CHECK(!exists(subject));

    // Rename back
    BOOST_CHECK_EQUAL(
        provider->rename(Consumer().get(), new_name.in(), old_name.in()),
        VARIANT_FALSE);
    BOOST_CHECK(!exists(target));
    BOOST_CHECK(exists(subject));
    BOOST_CHECK(is_directory(subject));
}

/**
 * This differs from the file version of the test in that obstructing
 * directories are harder to delete because they may have contents.
 * This test exercises that harder situation by adding a file to the
 * obstructing directory.
 *
 * TODO: Check the subject directory contents remains after renaming.
 */
BOOST_AUTO_TEST_CASE( rename_directory_with_obstruction )
{
    com_ptr<MockConsumer> consumer = Consumer();
    consumer->set_confirm_overwrite_behaviour(MockConsumer::AllowOverwrite);

    wpath subject = NewDirectoryInSandbox();

    // Obstruct renaming by creating an empty file on the target location
    wpath target = NewDirectoryInSandbox(subject.filename() + L"renamed");

    // Swish creates a temporary for non-atomic overwrite to minimise the
    // chance of failing to rename but losing the overwritten file as well.
    // We need to check this gets removed correctly.
    wpath swish_rename_temp_file =
        target.parent_path() / (target.filename() + L".swish_rename_temp");

    // Check that the non-atomic overwrite temp does not already exist
    BOOST_CHECK(!exists(swish_rename_temp_file));

    // Add a file in the obstructing directory to make it harder to delete
    wpath target_contents = target / L"somefile";
    ofstream(target_contents).close();

    BOOST_CHECK_EQUAL(
        Provider()->rename(
            consumer.in(), bstr_t(subject.string()).in(),
            bstr_t(target.string()).in()),
        VARIANT_TRUE);

    // The consumer should have been prompted for permission
    BOOST_CHECK(consumer->was_asked_to_confirm_overwrite());

    // Check that the old file no longer exists but the target does
    BOOST_CHECK(!exists(subject));
    BOOST_CHECK(exists(target));

    // Check that the non-atomic overwrite temp has been removed
    BOOST_CHECK(!exists(swish_rename_temp_file));
}

/**
 * TODO: check the contents of the target directory to make sure it's untouched.
 */
BOOST_AUTO_TEST_CASE(
    rename_directory_with_obstruction_refused_overwrite_permission )
{
    com_ptr<MockConsumer> consumer = Consumer();
    consumer->set_confirm_overwrite_behaviour(MockConsumer::PreventOverwrite);

    wpath subject = NewDirectoryInSandbox();
    // Obstruct renaming by creating an empty file on the target location
    wpath target = NewDirectoryInSandbox(subject.filename() + L"renamed");

    BOOST_CHECK_EXCEPTION(
        Provider()->rename(
            consumer.in(), bstr_t(subject.string()).in(),
            bstr_t(target.string()).in()),
        com_error, is_abort);

    // The consumer should have been prompted for permission
    BOOST_CHECK(consumer->was_asked_to_confirm_overwrite());

    // Check that both files still exist
    BOOST_CHECK(exists(subject));
    BOOST_CHECK(exists(target));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( deleting_tests )

/**
 * Delete a file and ensure other files in the same folder aren't also removed.
 */
BOOST_AUTO_TEST_CASE( delete_file )
{
    wpath file_before = NewFileInSandbox();
    wpath file = NewFileInSandbox();
    wpath file_after = NewFileInSandbox();

    Provider()->remove_all(bstr_t(ToRemotePath(file).string()).in());

    BOOST_CHECK(exists(file_before));
    BOOST_CHECK(!exists(file));
    BOOST_CHECK(exists(file_after));
}

/**
 * Delete a file with a unicode filename.
 */
BOOST_AUTO_TEST_CASE( delete_unicode_file )
{
    wpath unicode_file_name = NewFileInSandbox(L"العربية.txt");

    Provider()->remove_all(
        bstr_t(ToRemotePath(unicode_file_name).string()).in());

    BOOST_CHECK(!exists(unicode_file_name));
}

/**
 * Delete an empty directory.
 */
BOOST_AUTO_TEST_CASE( delete_empty_directory )
{
    wpath directory = Sandbox() / L"العربية";
    create_directory(directory);

    Provider()->remove_all(bstr_t(ToRemotePath(directory).string()).in());

    BOOST_CHECK(!exists(directory));
}

/**
 * Delete a non-empty directory.  This is trickier as the contents have to be
 * deleted before the directory.
 */
BOOST_AUTO_TEST_CASE( delete_directory_recursively )
{
    wpath directory = NewDirectoryInSandbox(L"العربية");
    wpath file = directory / L"русский.txt";
    ofstream(file).close();

    Provider()->remove_all(bstr_t(ToRemotePath(directory).string()).in());

    BOOST_CHECK(!exists(directory));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( file_creation_tests )

/**
 * Create a directory with a unicode filename.
 */
BOOST_AUTO_TEST_CASE( create_directory )
{
    wpath file = Sandbox() / L"漢字 العربية русский 47";
    BOOST_CHECK(!exists(file));

    Provider()->create_new_directory(bstr_t(ToRemotePath(file).string()).in());

    BOOST_CHECK(exists(file));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE( stream_creation_tests )

/**
 * Create a stream to a file with a unicode filename.
 *
 * Tests file creation as we don't create the file before the call and we
 * check that it exists after.
 */
BOOST_AUTO_TEST_CASE( get_file_stream )
{
    wpath file = Sandbox() / L"漢字 العربية русский.txt";
    BOOST_CHECK(!exists(file));

    com_ptr<IStream> stream = Provider()->get_file(
        ToRemotePath(file).string(), std::ios_base::out);

    BOOST_CHECK(stream);
    BOOST_CHECK(exists(file));

    STATSTG statstg = STATSTG();
    HRESULT hr = stream->Stat(&statstg, STATFLAG_DEFAULT);
    BOOST_CHECK_EQUAL(statstg.pwcsName, file.filename());
    ::CoTaskMemFree(statstg.pwcsName);
    BOOST_REQUIRE_OK(hr);
}

/**
 * Try to get a read-only stream to a non-existent file.
 *
 * This must fail as out DropTarget uses it to check whether the file already
 * exists.
 */
BOOST_AUTO_TEST_CASE( get_file_stream_fail )
{
    wpath file = Sandbox() / L"漢字 العربية русский.txt";
    BOOST_CHECK(!exists(file));

    BOOST_CHECK_THROW(
        Provider()->get_file(ToRemotePath(file).string(), std::ios_base::in),
        exception);

    BOOST_CHECK(!exists(file));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE_END()
