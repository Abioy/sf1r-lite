/* 
 * File:   t_ScdIndex.cpp
 * Author: Paolo D'Apice
 *
 * Created on September 11, 2012, 10:02 AM
 */

#include <boost/test/unit_test.hpp>

#include "ScdBuilder.h"
#include "common/ScdIndex.h"
#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

struct TestFixture {
    TestFixture() {}
    fs::path createScd(const std::string& dir, const std::string& name, 
            const unsigned size, const unsigned start = 1) const {
        fs::path path = createTempDir(dir)/name;
        ScdBuilder scd(path);
        for (unsigned i = start; i < start + size; ++i) {
            scd("DOCID") << i;
            scd("Title") << "Title " << i;
            scd("Content") << "Content A";
        }        
        return path;
    }
    fs::path tempFile(const std::string& name) const {
        fs::path file = fs::temp_directory_path()/name;
        fs::remove_all(file);
        BOOST_REQUIRE(not fs::exists(file));
        return file;
    }
private:
    fs::path createTempDir(const std::string& name) const {
        fs::path dir = fs::temp_directory_path()/name;
        fs::create_directories(dir);
        return dir;
    }
};

#define DOCID(X) izenelib::util::UString((#X), izenelib::util::UString::UTF_8)

SCD_INDEX_PROPERTY_TAG(Title, "Title")

BOOST_FIXTURE_TEST_CASE(test_index, TestFixture) {
    const unsigned DOC_NUM = 10;
    fs::path path = createScd("index", "test.scd", DOC_NUM);
    
    // build index
    scd::ScdIndex<Title>* indexptr;
    BOOST_REQUIRE_NO_THROW(indexptr = scd::ScdIndex<Title>::build(path.string()));

    // using const reference
    const scd::ScdIndex<Title>& index = *indexptr;
    BOOST_CHECK_EQUAL(DOC_NUM, index.size());

    std::cout << "Index by DOCID: " << std::endl;
    std::copy(index.begin<scd::docid>(), index.end<scd::docid>(), 
              std::ostream_iterator<scd::Document<Title> >(std::cout, "\n"));

    std::cout << "Index by property: " << std::endl;
    std::copy(index.begin<Title>(), index.end<Title>(), 
              std::ostream_iterator<scd::Document<Title> >(std::cout, "\n"));

    // query: hit
    BOOST_CHECK_EQUAL(  0, index.find<scd::docid>(DOCID(1))->offset);
    BOOST_CHECK_EQUAL( 50, index.find<scd::docid>(DOCID(2))->offset);
    BOOST_CHECK_EQUAL( 93, index.find<scd::docid>(DOCID(3))->offset);
    BOOST_CHECK_EQUAL(136, index.find<scd::docid>(DOCID(4))->offset);
    BOOST_CHECK_EQUAL(179, index.find<scd::docid>(DOCID(5))->offset);
    BOOST_CHECK_EQUAL(222, index.find<scd::docid>(DOCID(6))->offset);
    BOOST_CHECK_EQUAL(265, index.find<scd::docid>(DOCID(7))->offset);
    BOOST_CHECK_EQUAL(308, index.find<scd::docid>(DOCID(8))->offset);
    BOOST_CHECK_EQUAL(351, index.find<scd::docid>(DOCID(9))->offset);
    BOOST_CHECK_EQUAL(394, index.find<scd::docid>(DOCID(10))->offset);
    BOOST_CHECK_EQUAL(1, index.count<scd::docid>(DOCID(1)));

    BOOST_CHECK_EQUAL(  0, index.find<Title>(DOCID(Title 1))->offset);
    BOOST_CHECK_EQUAL( 50, index.find<Title>(DOCID(Title 2))->offset);
    BOOST_CHECK_EQUAL( 93, index.find<Title>(DOCID(Title 3))->offset);
    BOOST_CHECK_EQUAL(136, index.find<Title>(DOCID(Title 4))->offset);
    BOOST_CHECK_EQUAL(179, index.find<Title>(DOCID(Title 5))->offset);
    BOOST_CHECK_EQUAL(222, index.find<Title>(DOCID(Title 6))->offset);
    BOOST_CHECK_EQUAL(265, index.find<Title>(DOCID(Title 7))->offset);
    BOOST_CHECK_EQUAL(308, index.find<Title>(DOCID(Title 8))->offset);
    BOOST_CHECK_EQUAL(351, index.find<Title>(DOCID(Title 9))->offset);
    BOOST_CHECK_EQUAL(394, index.find<Title>(DOCID(Title 10))->offset);
    BOOST_CHECK_EQUAL(1, index.count<Title>(DOCID(Title 1)));
    
    // query: miss
    scd::ScdIndex<Title>::docid_iterator docid_end = index.end<scd::docid>();
    BOOST_CHECK(docid_end == index.find<scd::docid>(DOCID(0)));
    BOOST_CHECK(docid_end == index.find<scd::docid>(DOCID(11)));

    scd::ScdIndex<Title>::property_iterator property_end = index.end<Title>();
    BOOST_CHECK(property_end == index.find<Title>(DOCID(Title 0)));
    BOOST_CHECK(property_end == index.find<Title>(DOCID(Title 11)));

    delete indexptr;
}

BOOST_FIXTURE_TEST_CASE(test_serialization, TestFixture) {
    fs::path path_a = createScd("serialization", "a.scd", 5);
    fs::path path_b = createScd("serialization", "b.scd", 3, 5);

    scd::ScdIndex<Title>* index = scd::ScdIndex<Title>::build(path_a.string());
    
    // save to file
    fs::path saved = tempFile("saved");
    index->save(saved.string());
    BOOST_CHECK(fs::exists(saved));
    
    // load from file
    {
        scd::ScdIndex<Title> loaded;
        loaded.load(saved.string());
        BOOST_CHECK_EQUAL(index->size(), loaded.size());
        BOOST_CHECK(std::equal(index->begin<scd::docid>(), index->end<scd::docid>(), 
                               loaded.begin<scd::docid>()));
    }

    // loading from file _replaces_ existing content.
    {
        scd::ScdIndex<Title>* index_b = scd::ScdIndex<Title>::build(path_b.string());
        fs::path saved_b = tempFile("saved-b");
        index_b->save(saved_b.string());

        index->load(saved_b.string());
        std::copy(index->begin<scd::docid>(), index->end<scd::docid>(), 
                  std::ostream_iterator<scd::Document<Title> >(std::cout, "\n"));

        BOOST_CHECK_EQUAL(index->size(), index_b->size());
        BOOST_CHECK(std::equal(index->begin<scd::docid>(), index->end<scd::docid>(), 
                               index_b->begin<scd::docid>()));
        delete index_b;
    }
    
    delete index;
}