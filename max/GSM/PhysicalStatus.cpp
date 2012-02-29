/**@file Declarations for PhysicalStatus and related classes. */

/*
* Copyright 2010, 2011 Free Software Foundation, Inc.
* Copyright 2010 Kestrel Signal Processing, Inc.
* Copyright 2011 Range Networks, Inc.
*
* This software is distributed under the terms of the GNU Affero Public License.
* See the COPYING file in the main directory for details.
*
* This use of this software may be subject to additional restrictions.
* See the LEGAL file in the main directory for details.

	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Affero General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Affero General Public License for more details.

	You should have received a copy of the GNU Affero General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/


#include "PhysicalStatus.h"
#include <Logger.h>
#include <Globals.h>
#include <sqlite3.h>
#include <sqlite3util.h>

#include <GSML3RRElements.h>
#include <GSMLogicalChannel.h>

#include <iostream>
#include <iomanip>
#include <math.h>
#include <string>

using namespace std;
using namespace GSM;


static const char* createPhysicalStatus = {
	"CREATE TABLE IF NOT EXISTS PHYSTATUS ("
		"CN_TN_TYPE_AND_OFFSET STRING PRIMARY KEY, "		// CnTn <chan>-<index>
		"ARFCN INTEGER DEFAULT NULL, "						// actual ARFCN
		"ACCESSED INTEGER DEFAULT 0, "						// Unix time of last update
		"RXLEV_FULL_SERVING_CELL INTEGER DEFAULT NULL, "	// from the most recent measurement report
		"RXLEV_SUB_SERVING_CELL INTEGER DEFAULT NULL, "		// from the most recent measurement report
		"RXQUAL_FULL_SERVING_CELL_BER FLOAT DEFAULT NULL, "	// from the most recent measurement report
		"RXQUAL_SUB_SERVING_CELL_BER FLOAT DEFAULT NULL, "	// from the most recent measurement report
		"RSSI FLOAT DEFAULT NULL, "							// RSSI relative to full scale input
		"TIME_ERR FLOAT DEFAULT NULL, "						// timing advance error in symbol periods
		"TRANS_PWR INTEGER DEFAULT NULL, "					// handset tx power in dBm
		"TIME_ADVC INTEGER DEFAULT NULL, "					// handset timing advance in symbol periods
		"FER FLOAT DEFAULT NULL "							// uplink FER
		"NO_NCELL INTEGER DEFAULT NULL, "			// !!!NEW : 0...6, Anzahl der Nachbarzellen
		"RXLEV_CELL_1 INTEGER DEFAULT NULL, "			// !!!NEW : Empfangspegel der Nachbarzelle 1
		"BCCH_FREQ_CELL_1 INTEGER DEFAULT NULL, "		// !!!NEW : 0...31, Broadcast Channel Freq. der Nachbarzelle 1
		"BSIC_CELL_1 INTEGER DEFAULT NULL, "			// !!!NEW : 6-bit, Base Station ID Code der Nachbarzelle 1
		"RXLEV_CELL_2 INTEGER DEFAULT NULL, "			// !!!NEW : Empfangspegel der Nachbarzelle 2
		"BCCH_FREQ_CELL_2 INTEGER DEFAULT NULL, "		// !!!NEW : 0...31, Broadcast Channel Freq. der Nachbarzelle 2
		"BSIC_CELL_2 INTEGER DEFAULT NULL, "			// !!!NEW : 6-bit, Base Station ID Code der Nachbarzelle 2
		"RXLEV_CELL_3 INTEGER DEFAULT NULL, "			// !!!NEW : Empfangspegel der Nachbarzelle 3
		"BCCH_FREQ_CELL_3 INTEGER DEFAULT NULL, "		// !!!NEW : 0...31, Broadcast Channel Freq. der Nachbarzelle 3
		"BSIC_CELL_3 INTEGER DEFAULT NULL, "			// !!!NEW : 6-bit, Base Station ID Code der Nachbarzelle 3
		"RXLEV_CELL_4 INTEGER DEFAULT NULL, "			// !!!NEW : Empfangspegel der Nachbarzelle 4
		"BCCH_FREQ_CELL_4 INTEGER DEFAULT NULL, "		// !!!NEW : 0...31, Broadcast Channel Freq. der Nachbarzelle 4
		"BSIC_CELL_4 INTEGER DEFAULT NULL, "			// !!!NEW : 6-bit, Base Station ID Code der Nachbarzelle 4
		"RXLEV_CELL_5 INTEGER DEFAULT NULL, "			// !!!NEW : Empfangspegel der Nachbarzelle 5
		"BCCH_FREQ_CELL_5 INTEGER DEFAULT NULL, "		// !!!NEW : 0...31, Broadcast Channel Freq. der Nachbarzelle 5
		"BSIC_CELL_5 INTEGER DEFAULT NULL, "			// !!!NEW : 6-bit, Base Station ID Code der Nachbarzelle 5
		"RXLEV_CELL_6 INTEGER DEFAULT NULL, "			// !!!NEW : Empfangspegel der Nachbarzelle 6
		"BCCH_FREQ_CELL_6 INTEGER DEFAULT NULL, "		// !!!NEW : 0...31, Broadcast Channel Freq. der Nachbarzelle 6
		"BSIC_CELL_6 INTEGER DEFAULT NULL, "			// !!!NEW : 6-bit, Base Station ID Code der Nachbarzelle 6
	")"
};

int PhysicalStatus::open(const char* wPath)
{
	int rc = sqlite3_open(wPath, &mDB);
	if (rc) {
		LOG(EMERG) << "Cannot open PhysicalStatus database at " << wPath << ": " << sqlite3_errmsg(mDB);
		sqlite3_close(mDB);
		mDB = NULL;
		return 1;
	}
	if (!sqlite3_command(mDB, createPhysicalStatus)) {
		LOG(EMERG) << "Cannot create TMSI table";
		return 1;
	}
	return 0;
}

PhysicalStatus::~PhysicalStatus()
{
	if (mDB) sqlite3_close(mDB);
}

bool PhysicalStatus::createEntry(const LogicalChannel* chan)
{
	assert(mDB);
	assert(chan);

	ScopedLock lock(mLock);

	const char* chanString = chan->descriptiveString();
	LOG(DEBUG) << chan->descriptiveString();

	/* Check to see if the key exists. */
	if (!sqlite3_exists(mDB, "PHYSTATUS", "CN_TN_TYPE_AND_OFFSET", chanString)) {
		/* No? Ok, it should now. */
		char query[500];
		sprintf(query, "INSERT INTO PHYSTATUS (CN_TN_TYPE_AND_OFFSET, ACCESSED) VALUES "
					   "(\"%s\", %u)",
				chanString, (unsigned)time(NULL));
		return sqlite3_command(mDB, query);
	}

	return false;
}

bool PhysicalStatus::setPhysical(const LogicalChannel* chan,
								const L3MeasurementResults& measResults)
{
	// TODO -- It would be better if the argument what just the channel
	// and the key was just the descriptiveString.

	assert(mDB);
	assert(chan);

	ScopedLock lock(mLock);

	createEntry(chan);
	
	// Measurementinfos der Nachbarzellen
	unsigned mRXLEV_NCELL_sql[6] = {};
	unsigned mBCCH_FREQ_NCELL_sql[6] = {};
	unsigned mBSIC_NCELL_sql[6] = {};
	
	if (measResults.NO_NCELL() != 7){	// we have a neighbor list?
	  for(unsigned i=0; i<measResults.NO_NCELL(); i++){
	    mRXLEV_NCELL_sql[i] = measResults.RXLEV_NCELL(i);
	    mBCCH_FREQ_NCELL_sql[i] = measResults.BCCH_FREQ_NCELL(i);
	    mBSIC_NCELL_sql[i] = measResults.BSIC_NCELL(i);
	  }
	}

	char query[1000];
	
	sprintf(query,
		"UPDATE PHYSTATUS SET "
		"NO_NCELL=%u, "
		"RXLEV_CELL_1=%d, BCCH_FREQ_CELL_1=%u, BSIC_CELL_1=%u, "
		"RXLEV_CELL_2=%d, BCCH_FREQ_CELL_2=%u, BSIC_CELL_2=%u, "
		"RXLEV_CELL_3=%d, BCCH_FREQ_CELL_3=%u, BSIC_CELL_3=%u, "
		"RXLEV_CELL_4=%d, BCCH_FREQ_CELL_4=%u, BSIC_CELL_4=%u, "
		"RXLEV_CELL_5=%d, BCCH_FREQ_CELL_5=%u, BSIC_CELL_5=%u, "
		"RXLEV_CELL_6=%d, BCCH_FREQ_CELL_6=%u, BSIC_CELL_6=%u, "
		"RXLEV_FULL_SERVING_CELL=%d, "
		"RXLEV_SUB_SERVING_CELL=%d, "
		"RXQUAL_FULL_SERVING_CELL_BER=%f, "
		"RXQUAL_SUB_SERVING_CELL_BER=%f, "
		"RSSI=%f, "
		"TIME_ERR=%f, "
		"TRANS_PWR=%u, "
		"TIME_ADVC=%u, "
		"FER=%f, "
		"ACCESSED=%u, "
		"ARFCN=%u "
		"WHERE CN_TN_TYPE_AND_OFFSET==\"%s\"",
		measResults.NO_NCELL(),
		mRXLEV_NCELL_sql[1], mBCCH_FREQ_NCELL_sql[1], mBSIC_NCELL_sql[1],
		mRXLEV_NCELL_sql[2], mBCCH_FREQ_NCELL_sql[2], mBSIC_NCELL_sql[2],
		mRXLEV_NCELL_sql[3], mBCCH_FREQ_NCELL_sql[3], mBSIC_NCELL_sql[3],
		mRXLEV_NCELL_sql[4], mBCCH_FREQ_NCELL_sql[4], mBSIC_NCELL_sql[4],
		mRXLEV_NCELL_sql[5], mBCCH_FREQ_NCELL_sql[5], mBSIC_NCELL_sql[5],
		mRXLEV_NCELL_sql[6], mBCCH_FREQ_NCELL_sql[6], mBSIC_NCELL_sql[6],
		measResults.RXLEV_FULL_SERVING_CELL_dBm(),
		measResults.RXLEV_SUB_SERVING_CELL_dBm(),
		measResults.RXQUAL_FULL_SERVING_CELL_BER(),
		measResults.RXQUAL_SUB_SERVING_CELL_BER(),
		chan->RSSI(), chan->timingError(),
		chan->actualMSPower(), chan->actualMSTiming(),
		chan->FER(),
		(unsigned)time(NULL),
		chan->ARFCN(),
		chan->descriptiveString());

	LOG(DEBUG) << "Query: " << query;

	return sqlite3_command(mDB, query);
}

#if 0
void PhysicalStatus::dump(ostream& os) const
{
	sqlite3_stmt *stmt;
	sqlite3_prepare_statement(mDB, &stmt,
		"SELECT CN,TN,TYPE_AND_OFFSET,FER,RSSI,TRANS_PWR,TIME_ADVC,RXLEV_FULL_SERVING_CELL,RXQUAL_FULL_SERVING_CELL_BER FROM PHYSTATUS");

	while (sqlite3_run_query(mDB,stmt) == SQLITE_ROW) {
		os << setw(2) << sqlite3_column_int(stmt, 0) << " " << sqlite3_column_int(stmt, 1);
		os << " " << setw(9) << (TypeAndOffset)sqlite3_column_int(stmt, 2);

		char buffer[1024];
		sprintf(buffer, "%10d %5.2f %4d %5d %4d",
			sqlite3_column_int(stmt, 3),
			100.0*sqlite3_column_double(stmt, 4), (int)round(sqlite3_column_double(stmt, 5)),
			sqlite3_column_int(stmt, 6), sqlite3_column_int(stmt, 7));
		os << " " << buffer;

		sprintf(buffer, "%5d %5.2f",
			sqlite3_column_int(stmt, 8), 100.0*sqlite3_column_double(stmt, 9));
		os << " " << buffer;

		os << endl;
	}
	sqlite3_finalize(stmt);
}
#endif

void PhysicalStatus::dump(ostream& os) const
{
	sqlite3_stmt *stmt;
	sqlite3_prepare_statement(mDB, &stmt,
		"SELECT CN_TN_TYPE_AND_OFFSET,ARFCN,ACCESSED,RSSI,TIME_ERR,TIME_ADVC,"	// 0 1 2 3 4 5
		"TRANS_PWR,FER,RXLEV_FULL_SERVING_CELL,RXLEV_SUB_SERVING_CELL,"		// 6 7 8 9
		"RXQUAL_FULL_SERVING_CELL_BER,RXQUAL_SUB_SERVING_CELL,NO_CELL,"		// 10 11 12
		"RXLEV_CELL_1,BCCH_FREQ_CELL_1,BSIC_CELL_1,"				// 13 14 15
		"RXLEV_CELL_2,BCCH_FREQ_CELL_2,BSIC_CELL_2,"				// 16 17 18
		"RXLEV_CELL_3,BCCH_FREQ_CELL_3,BSIC_CELL_3,"				// 19 20 21
		"RXLEV_CELL_4,BCCH_FREQ_CELL_4,BSIC_CELL_4,"				// 22 23 24
		"RXLEV_CELL_5,BCCH_FREQ_CELL_5,BSIC_CELL_5,"				// 25 26 27
		"RXLEV_CELL_6,BCCH_FREQ_CELL_6,BSIC_CELL_6 FROM PHYSTATUS");		// 28 29 30
	
	os << "##################################################################" << endl;
	os << "\t\tMeasurement Report:" << endl;
	os << "##################################################################" << endl;

	while (sqlite3_run_query(mDB,stmt) == SQLITE_ROW) {
			
	        os << "CN_TN_TYPE_OFFSET\t\t=\t" << (TypeAndOffset)sqlite3_column_int(stmt, 0) << endl;
		os << "ARFCN\t\t\t=\t" << sqlite3_column_int(stmt, 1) << endl;
		os << "ACCESSED\t\t\t=\t" << sqlite3_column_int(stmt, 2) << endl;
		os << "RSSI\t\t\t=\t" << sqlite3_column_double(stmt, 3) << endl;
		os << "TIME_ERR\t\t\t=\t" << sqlite3_column_double(stmt, 4) << endl;
		os << "TIME_ADVC\t\t\t=\t" << sqlite3_column_int(stmt, 5) << endl;
		os << "TRANS_PWR\t\t\t=\t" << sqlite3_column_int(stmt, 6) << " dBm" << endl;
		os << "FER\t\t\t=\t" << sqlite3_column_double(stmt, 7) << endl;
		os << "RXLEV_FULL_SERVING_CELL\t=\t" << sqlite3_column_int(stmt, 8) << " dBm" << endl;
		os << "RXLEV_SUB_SERVING_CELL\t\t=\t" << sqlite3_column_int(stmt, 9) << " dBm" << endl;
		os << "RXQUAL_FULL_SERVING_CELL_BER\t=\t" << sqlite3_column_double(stmt, 10) << " dBm" << endl;
		os << "RXQUAL_SUB_SERVING_CELL_BER\t=\t" << sqlite3_column_double(stmt, 11) << " dBm" << endl;        
		os << "NO_NCELL\t\t\t=\t" << sqlite3_column_int(stmt, 12) << endl;
		os << "RXLEV_CELL_1 = " << sqlite3_column_int(stmt, 13) << ", BCCH_FREQ_CELL_1 = " << sqlite3_column_int(stmt, 14) << ", BSIC_CELL_1 = " << sqlite3_column_int(stmt, 15) << endl;
		os << "RXLEV_CELL_2 = " << sqlite3_column_int(stmt, 16) << ", BCCH_FREQ_CELL_2 = " << sqlite3_column_int(stmt, 17) << ", BSIC_CELL_2 = " << sqlite3_column_int(stmt, 18) << endl;
		os << "RXLEV_CELL_3 = " << sqlite3_column_int(stmt, 19) << ", BCCH_FREQ_CELL_3 = " << sqlite3_column_int(stmt, 20) << ", BSIC_CELL_3 = " << sqlite3_column_int(stmt, 21) << endl;
		os << "RXLEV_CELL_4 = " << sqlite3_column_int(stmt, 22) << ", BCCH_FREQ_CELL_4 = " << sqlite3_column_int(stmt, 23) << ", BSIC_CELL_4 = " << sqlite3_column_int(stmt, 24) << endl;
		os << "RXLEV_CELL_5 = " << sqlite3_column_int(stmt, 25) << ", BCCH_FREQ_CELL_5 = " << sqlite3_column_int(stmt, 26) << ", BSIC_CELL_5 = " << sqlite3_column_int(stmt, 27) << endl;
		os << "RXLEV_CELL_6 = " << sqlite3_column_int(stmt, 28) << ", BCCH_FREQ_CELL_6 = " << sqlite3_column_int(stmt, 29) << ", BSIC_CELL_6 = " << sqlite3_column_int(stmt, 30) << endl;
		os << endl << "##################################################################" << endl;
		
	}
	sqlite3_finalize(stmt);
}

// vim: ts=4 sw=4
