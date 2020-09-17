#include "mining/journal_change_set.h"
#include "mempooltxdb.h"

#include "mempool_test_access.h"

#include "test/test_bitcoin.h"

#include <boost/test/unit_test.hpp>

#include <vector>

namespace {
    mining::CJournalChangeSetPtr nullChangeSet{nullptr};

    std::vector<CTxMemPoolEntry> GetABunchOfEntries(int howMany)
    {
        TestMemPoolEntryHelper entry;
        std::vector<CTxMemPoolEntry> result;
        for (int i = 0; i < howMany; i++) {
            CMutableTransaction mtx;
            mtx.vin.resize(1);
            mtx.vin[0].scriptSig = CScript() << OP_11;
            mtx.vout.resize(1);
            mtx.vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
            mtx.vout[0].nValue = Amount(33000LL + i);
            result.emplace_back(entry.FromTx(mtx));
        }
        return result;
    }
}

BOOST_FIXTURE_TEST_SUITE(mempooltxdb_tests, TestingSetup)
BOOST_AUTO_TEST_CASE(WriteToTxDB)
{
    const auto entries = GetABunchOfEntries(11);

    CMempoolTxDB txdb(10000);
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0);

    // Write the entries to the database.
    uint64_t totalSize = 0;
    for (const auto& e : entries)
    {
        totalSize += e.GetTxSize();
        BOOST_CHECK(txdb.AddTransaction(e.GetTxId(), e.GetSharedTx()));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize);

    // Check that all transactions are in the databas.
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(txdb.GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(DoubleWriteToTxDB)
{
    const auto entries = GetABunchOfEntries(13);

    CMempoolTxDB txdb(10000);
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0);

    // Write the entries to the database.
    uint64_t totalSize = 0;
    for (const auto& e : entries)
    {
        totalSize += e.GetTxSize();
        BOOST_CHECK(txdb.AddTransaction(e.GetTxId(), e.GetSharedTx()));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize);

    // Check that all transactions are in the databas.
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(txdb.GetTransaction(e.GetTxId(), _));
    }

    // Write and check again.
    for (const auto& e : entries)
    {
        BOOST_CHECK(txdb.AddTransaction(e.GetTxId(), e.GetSharedTx()));
    }
    BOOST_WARN_EQUAL(txdb.GetDiskUsage(), totalSize);
    BOOST_CHECK_GE(txdb.GetDiskUsage(), totalSize);
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(txdb.GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(DeleteFromTxDB)
{
    const auto entries = GetABunchOfEntries(17);

    CMempoolTxDB txdb(10000);
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0);

    // Write the entries to the database.
    uint64_t totalSize = 0;
    for (const auto& e : entries)
    {
        totalSize += e.GetTxSize();
        BOOST_CHECK(txdb.AddTransaction(e.GetTxId(), e.GetSharedTx()));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize);

    // Remove transactions from the database one by one.
    for (const auto& e : entries)
    {
        std::vector<uint256> txids{e.GetTxId()};
        BOOST_CHECK(txdb.RemoveTransactions(txids, e.GetTxSize()));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0);
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(!txdb.GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(BatchDeleteFromTxDB)
{
    const auto entries = GetABunchOfEntries(19);

    CMempoolTxDB txdb(10000);
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0);

    // Write the entries to the database.
    uint64_t totalSize = 0;
    std::vector<uint256> txids;
    for (const auto& e : entries)
    {
        totalSize += e.GetTxSize();
        txids.emplace_back(e.GetTxId());
        BOOST_CHECK(txdb.AddTransaction(e.GetTxId(), e.GetSharedTx()));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize);

    // Remove all transactions from the database at once.
    BOOST_CHECK(txdb.RemoveTransactions(txids, totalSize));
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0);
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(!txdb.GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(ClearTxDB)
{
    const auto entries = GetABunchOfEntries(23);

    CMempoolTxDB txdb(10000);
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0);

    // Write the entries to the database.
    uint64_t totalSize = 0;
    for (const auto& e : entries)
    {
        totalSize += e.GetTxSize();
        BOOST_CHECK(txdb.AddTransaction(e.GetTxId(), e.GetSharedTx()));
    }
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), totalSize);

    // Clear the database and check that it's empty.
    txdb.ClearDatabase();
    BOOST_CHECK_EQUAL(txdb.GetDiskUsage(), 0);
    for (const auto& e : entries)
    {
        CTransactionRef _;
        BOOST_CHECK(!txdb.GetTransaction(e.GetTxId(), _));
    }
}

BOOST_AUTO_TEST_CASE(SaveOnFullMempool)
{
    TestMemPoolEntryHelper entry;
    // Parent transaction with three children, and three grand-children:
    CMutableTransaction txParent;
    txParent.vin.resize(1);
    txParent.vin[0].scriptSig = CScript() << OP_11;
    txParent.vout.resize(3);
    for (int i = 0; i < 3; i++) {
        txParent.vout[i].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txParent.vout[i].nValue = Amount(33000LL);
    }
    CMutableTransaction txChild[3];
    for (int i = 0; i < 3; i++) {
        txChild[i].vin.resize(1);
        txChild[i].vin[0].scriptSig = CScript() << OP_11;
        txChild[i].vin[0].prevout = COutPoint(txParent.GetId(), i);
        txChild[i].vout.resize(1);
        txChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txChild[i].vout[0].nValue = Amount(11000LL);
    }
    CMutableTransaction txGrandChild[3];
    for (int i = 0; i < 3; i++) {
        txGrandChild[i].vin.resize(1);
        txGrandChild[i].vin[0].scriptSig = CScript() << OP_11;
        txGrandChild[i].vin[0].prevout = COutPoint(txChild[i].GetId(), 0);
        txGrandChild[i].vout.resize(1);
        txGrandChild[i].vout[0].scriptPubKey = CScript() << OP_11 << OP_EQUAL;
        txGrandChild[i].vout[0].nValue = Amount(11000LL);
    }

    CTxMemPool testPool;
    CTxMemPoolTestAccess testPoolAccess(testPool);

    // Nothing in pool, remove should do nothing:
    BOOST_CHECK_EQUAL(testPool.Size(), 0);
    testPool.SaveTxsToDisk(10000);
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), 0);
    BOOST_CHECK_EQUAL(testPool.Size(), 0);

    // Add transactions:
    testPool.AddUnchecked(txParent.GetId(), entry.FromTx(txParent), nullChangeSet);
    for (int i = 0; i < 3; i++) {
        testPool.AddUnchecked(txChild[i].GetId(), entry.FromTx(txChild[i]), nullChangeSet);
        testPool.AddUnchecked(txGrandChild[i].GetId(), entry.FromTx(txGrandChild[i]), nullChangeSet);
    }

    // Saving transactions to disk doesn't change the mempool size:
    const auto poolSize = testPool.Size();
    testPool.SaveTxsToDisk(10000);
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize);

    // But it does store something to disk:
    const auto diskUsage = testPool.GetDiskUsage();
    BOOST_CHECK_GT(diskUsage, 0);

    // Check that all transactions have been saved to disk:
    uint64_t sizeTXsAdded = 0;
    for (const auto& entry : testPoolAccess.mapTx().get<entry_time>())
    {
        BOOST_CHECK(!entry.IsInMemory());
        sizeTXsAdded += entry.GetTxSize();
    }
    BOOST_CHECK_EQUAL(diskUsage, sizeTXsAdded);
}

BOOST_AUTO_TEST_CASE(RemoveFromDiskOnMempoolTrim)
{
    const auto entries = GetABunchOfEntries(6);

    CTxMemPool testPool;

    // Add transactions:
    for (auto& entry : entries) {
        testPool.AddUnchecked(entry.GetTxId(), entry, nullChangeSet);
    }

    // Saving transactions to disk doesn't change the mempool size:
    const auto poolSize = testPool.Size();
    BOOST_CHECK_EQUAL(poolSize, entries.size());
    testPool.SaveTxsToDisk(10000);
    BOOST_CHECK_EQUAL(testPool.Size(), poolSize);

    // But it does store something to disk:
    BOOST_CHECK_GT(testPool.GetDiskUsage(), 0);

    // Trimming the mempool size should also remove transactions from disk:
    testPool.TrimToSize(0, nullChangeSet);
    BOOST_CHECK_EQUAL(testPool.Size(), 0);
    BOOST_CHECK_EQUAL(testPool.GetDiskUsage(), 0);
}
BOOST_AUTO_TEST_SUITE_END()
