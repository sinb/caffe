#include <glog/logging.h>
#ifdef USE_LEVELDB
#include <leveldb/db.h>
#endif
#ifdef USE_LMDB
#include <lmdb.h>
#endif
#include <stdint.h>

#include <algorithm>
#include <string>

#include "caffe/proto/caffe.pb.h"
#include "caffe/util/io.hpp"

using caffe::Datum;
using caffe::BlobProto;
using std::string;
using std::max;

int main(int argc, char** argv) {
	::google::InitGoogleLogging(argv[0]);
	if (argc < 3 || argc > 4) {
		LOG(ERROR) << "Usage: compute_image_mean input_db output_file"
			<< " db_backend[leveldb or lmdb]";
		return 1;
	}

	string db_backend = "lmdb";
	if (argc == 4) {
		db_backend = string(argv[3]);
	}

#ifdef USE_LEVELDB
	// leveldb
	leveldb::DB* db = NULL;
	leveldb::Options options;
	options.create_if_missing = false;
	leveldb::Iterator* it = NULL;
#else
	if (db_backend == "leveldb")
		LOG(FATAL) << "USE_LEVELDB is turned off ";
#endif

#ifdef USE_LMDB
	// lmdb
	MDB_env* mdb_env;
	MDB_dbi mdb_dbi;
	MDB_val mdb_key, mdb_value;
	MDB_txn* mdb_txn;
	MDB_cursor* mdb_cursor;
#else
	if (db_backend == "lmdb")
		LOG(FATAL) << "USE_LMDB is turned off ";
#endif

	// Open db
	if (db_backend == "leveldb") {  // leveldb
#ifdef USE_LEVELDB
		LOG(INFO) << "Opening leveldb " << argv[1];
		leveldb::Status status = leveldb::DB::Open(
			options, argv[1], &db);
		CHECK(status.ok()) << "Failed to open leveldb " << argv[1];
		leveldb::ReadOptions read_options;
		read_options.fill_cache = false;
		it = db->NewIterator(read_options);
		it->SeekToFirst();
#endif
	} else if (db_backend == "lmdb") {  // lmdb
#ifdef USE_LMDB
		LOG(INFO) << "Opening lmdb " << argv[1];
		CHECK_EQ(mdb_env_create(&mdb_env), MDB_SUCCESS) << "mdb_env_create failed";
		CHECK_EQ(mdb_env_set_mapsize(mdb_env, 1099511627776), MDB_SUCCESS);  // 1TB
		CHECK_EQ(mdb_env_open(mdb_env, argv[1], MDB_RDONLY, 0664),
			MDB_SUCCESS) << "mdb_env_open failed";
		CHECK_EQ(mdb_txn_begin(mdb_env, NULL, MDB_RDONLY, &mdb_txn), MDB_SUCCESS)
			<< "mdb_txn_begin failed";
		CHECK_EQ(mdb_open(mdb_txn, NULL, 0, &mdb_dbi), MDB_SUCCESS)
			<< "mdb_open failed";
		CHECK_EQ(mdb_cursor_open(mdb_txn, mdb_dbi, &mdb_cursor), MDB_SUCCESS)
			<< "mdb_cursor_open failed";
		CHECK_EQ(mdb_cursor_get(mdb_cursor, &mdb_key, &mdb_value, MDB_FIRST),
			MDB_SUCCESS);

#endif
	} else {
		LOG(FATAL) << "Unknown db backend " << db_backend;
	}

	Datum datum;
	BlobProto sum_blob;
	int count = 0;
	// load first datum
	if (db_backend == "leveldb") {
#ifdef USE_LEVELDB
		datum.ParseFromString(it->value().ToString());
#endif
	} else if (db_backend == "lmdb") {
#ifdef USE_LMDB
		datum.ParseFromArray(mdb_value.mv_data, mdb_value.mv_size);
#endif
	} else {
		LOG(FATAL) << "Unknown db backend " << db_backend;
	}

	sum_blob.set_num(1);
	sum_blob.set_channels(datum.channels());
	sum_blob.set_height(datum.height());
	sum_blob.set_width(datum.width());
	const int data_size = datum.channels() * datum.height() * datum.width();
	int size_in_datum = std::max<int>(datum.data().size(),
		datum.float_data_size());
	for (int i = 0; i < size_in_datum; ++i) {
		sum_blob.add_data(0.);
	}
	LOG(INFO) << "Starting Iteration";
	if (db_backend == "leveldb") {  // leveldb
#ifdef USE_LEVELDB
		for (it->SeekToFirst(); it->Valid(); it->Next()) {
			// just a dummy operation
			datum.ParseFromString(it->value().ToString());
			const string& data = datum.data();
			size_in_datum = std::max<int>(datum.data().size(),
				datum.float_data_size());
			CHECK_EQ(size_in_datum, data_size) << "Incorrect data field size " <<
				size_in_datum;
			if (data.size() != 0) {
				for (int i = 0; i < size_in_datum; ++i) {
					sum_blob.set_data(i, sum_blob.data(i) + (uint8_t)data[i]);
				}
			} else {
				for (int i = 0; i < size_in_datum; ++i) {
					sum_blob.set_data(i, sum_blob.data(i) +
						static_cast<float>(datum.float_data(i)));
				}
			}
			++count;
			if (count % 10000 == 0) {
				LOG(ERROR) << "Processed " << count << " files.";
			}
		}
#endif
	} else if (db_backend == "lmdb") {  // lmdb
#ifdef USE_LMDB
		CHECK_EQ(mdb_cursor_get(mdb_cursor, &mdb_key, &mdb_value, MDB_FIRST),
			MDB_SUCCESS);
		do {
			// just a dummy operation
			datum.ParseFromArray(mdb_value.mv_data, mdb_value.mv_size);
			const string& data = datum.data();
			size_in_datum = std::max<int>(datum.data().size(),
				datum.float_data_size());
			CHECK_EQ(size_in_datum, data_size) << "Incorrect data field size " <<
				size_in_datum;
			if (data.size() != 0) {
				for (int i = 0; i < size_in_datum; ++i) {
					sum_blob.set_data(i, sum_blob.data(i) + (uint8_t)data[i]);
				}
			} else {
				for (int i = 0; i < size_in_datum; ++i) {
					sum_blob.set_data(i, sum_blob.data(i) +
						static_cast<float>(datum.float_data(i)));
				}
			}
			++count;
			if (count % 10000 == 0) {
				LOG(ERROR) << "Processed " << count << " files.";
			}
		} while (mdb_cursor_get(mdb_cursor, &mdb_key, &mdb_value, MDB_NEXT)
			== MDB_SUCCESS);
#endif
	} else {
		LOG(FATAL) << "Unknown db backend " << db_backend;
	}

	if (count % 10000 != 0) {
		LOG(ERROR) << "Processed " << count << " files.";
	}
	for (int i = 0; i < sum_blob.data_size(); ++i) {
		sum_blob.set_data(i, sum_blob.data(i) / count);
	}
	// Write to disk
	LOG(INFO) << "Write to " << argv[2];
	WriteProtoToBinaryFile(sum_blob, argv[2]);

	// Clean up
	if (db_backend == "leveldb") {
#ifdef USE_LEVELDB
		delete db;
#endif
	} else if (db_backend == "lmdb") {
#ifdef USE_LMDB
		mdb_cursor_close(mdb_cursor);
		mdb_close(mdb_env, mdb_dbi);
		mdb_txn_abort(mdb_txn);
		mdb_env_close(mdb_env);
#endif
	} else {
		LOG(FATAL) << "Unknown db backend " << db_backend;
	}
	return 0;
}
