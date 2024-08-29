//
// Created by main on 6/2/24.
//
#include "../include/QueryClass.h"
namespace Q {
    //getting started
    QueryClass::QueryClass(unsigned char &ec) : exitCode(ec) {
        /*file = freopen("ErrorOutput.txt", "w", stderr);
        if (file == nullptr) {
            std::cerr << "Error redirecting stderr to file." << std::endl;
            exitCode = 1;
        }*/
        //file = freopen("log.txt", "w", stdout);
        try {
            //open or create database
            int rc = sqlite3_open("db/timeData.db", &db);
            if (rc != SQLITE_OK) {
                throw (rc);
            }
        } catch (int ecode) {
            std::cerr << "CRITICAL ERROR\nCLOSING PROGRAM\n";
            handleError(ecode, "FAILED TO OPEN/CREATE DATABASE", "QueryClass()");
            exit(1);
        }
        buildTables();
        running = true;
        qLoop = std::thread(&QueryClass::requestLoop, this);
        data d;
        d.typeName = "Misc";
        d.table = TYPE;
        handleTraffic(d);
    }

    void QueryClass::buildTables() {
        const char* sql = R"(
            CREATE TABLE IF NOT EXISTS activity (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                active INTEGER NOT NULL
            );

            CREATE TABLE IF NOT EXISTS type (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE
            );

            CREATE TABLE IF NOT EXISTS class (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                type INTEGER NOT NULL,
                FOREIGN KEY (type) REFERENCES type(id)
            );

            CREATE TABLE IF NOT EXISTS program (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL UNIQUE,
                class INTEGER,
                FOREIGN KEY (class) REFERENCES class(id)
            );

            CREATE TABLE IF NOT EXISTS pTime (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                program INTEGER NOT NULL,
                utsStart INTEGER NOT NULL,
                timeUsed INTEGER,
                activity INTEGER NOT NULL,
                date TEXT NOT NULL,
                FOREIGN KEY (program) REFERENCES program(id),
                FOREIGN KEY (activity) REFERENCES activity(id)
            );
        )";
        char* errmsg = nullptr;
        try {
            int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
            if (rc != SQLITE_OK){
                throw (rc);
            }
            std::cout << "Tables successfully built\n";
        } catch (int ecode){
            std::string err(errmsg);
            sqlite3_free(errmsg);
            handleError(ecode, err, "buildTables()");
            return;
        }

    }

    void QueryClass::requestLoop() {
        while (running.load()) {
            data d = popQueue(); //stops here while waiting for the queue to not be empty
            Table t = d.table;
            if (t != END) { //end is only added to the queue when the program is shutting down
                if (d.request) {
                    storeDataDate(d.date);
                }
                else if (t == ACTIVITY)
                    handleActivity(d.activityName);
                else if (t == PROGRAM)
                    insertProgram(d);
                else if (t == TYPE)
                    insertType(d.typeName);
                else if (t == PTIME)
                    handlePTime(d);
            }
            else {
                editPtime(lastPTimeID, d.time);
            }
        }
    }

    //closing out
    QueryClass::~QueryClass() {
        sqlite3_close(db);
    }

    void QueryClass::endLoop() {
        if (running.load()) {
            running.store(false);
        }
        if (qLoop.joinable()) {
            data d;
            d.table = END;
            d.time = static_cast<int>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())); //this line gets the current unix timestamp which is of type time_point, it is then converted to time_t which is typically a long, and then it is static cast as an int
            addQueue(d);
        }
        qLoop.join();
    }

    //error handling
    void QueryClass::handleError(int ecode, std::string errmsg, const std::string &fName) {
        std::cerr << "Additional information for provided SQLite error codes may be found at\n"
                     "https://sqlite.org/rescode.html#ok\n";
        std::cerr << sqlite3_errmsg(db) << std::endl;
        switch (ecode) {
            case 1:
                std::cerr << "ERR Code (Q1): SQLITE_ERROR\n"
                             "ERR Msg: " << errmsg << "\n"
                             "Something went wrong. Code returned in " << fName
                          << std::endl;
                exitCode = 1;
                break;
            case 2:
                std::cerr << "ERR Code (Q2): SQLITE_INTERNAL\n"
                             "ERR Msg: " << errmsg << "\n"
                             "Bug in database engine. Code returned in " << fName
                          << std::endl;
                exitCode = 1;
                break;
            case 3:
                std::cerr << "ERR Code (Q3): SQLITE_PERM\n"
                             "ERR Msg: " << errmsg << "\n"
                             "The access mode for a newly created database could not be provided. Code returned"
                             " in " << fName << std::endl;
                exitCode = 1;
                break;
            case 4:
                std::cerr << "ERR Code (Q4): SQLITE_ABORT\n"
                             "ERR Msg: " << errmsg << "\n"
                             "An operation was aborted prior to completion. Code returned in exception block "
                             " in " << fName << std::endl;
                exitCode = 1;
                break;
            case 5:
                std::cerr << "ERR Code (Q5): SQLITE_BUSY\n"
                             "ERR Msg: " << errmsg << "\n"
                             "The database file could not be written or read from because of concurrent activity. Code returned"
                             " in " << fName << std::endl;
                break;
            case 6:
                std::cerr << "ERR Code (Q6): SQLITE_LOCKED\n"
                             "ERR Msg: " << errmsg << "\n"
                             "A write operation could not continue because of a conflict within the same database connection or a conflict "
                             "with a different database connection that uses a shared cache. Code returned"
                             " in " << fName << std::endl;
                break;
            case 19:
                std::cerr << "ERR Code (Q19): SQLITE_CONSTRAINT\n"
                             "ERR Msg: " << errmsg << "\n"
                             "Some sort of constraint was hit, this will most likely be a UNIQUE constraint which we expect to hit, there should be no worries with this error."
                             "Code returned in " << fName << std::endl;
                break;
            case 2067:
                std::cerr << "ERR Code (Q2067): SQLITE_CONSTRAINT_UNIQUE\n"
                             "ERR Msg: " << errmsg << "\n"
                             "An attempt to create a row using a duplicate unique value was made. This is nothing to worry about usually."
                             "Code returned in " << fName << std::endl;
                break;
            default:
                std::cerr <<"ERR Msg: " << errmsg << "\n"
                            "unfortunately haven't catalogued this error from " << fName << std::endl;
                exitCode = 1;
                break;

        }

    }

    //making requests
    void QueryClass::handleTraffic(data d) {
        addQueue(d);
    }

    bool QueryClass::activityChanged() {
        if (aChange){
            aChange = false;
            return true;
        }
        return false;
    }

    std::queue<outPTime> QueryClass::getData() {
        std::unique_lock<std::mutex> lock(rMutex);
        cvr.wait(lock,[this] {return !rData.empty();});
        std::cerr << "getData\n";
        std::queue<outPTime> temp = rData;
        while (!rData.empty()) {
            rData.pop();
        }
        return temp;
    }

    //thread safe queue and deque
    void QueryClass::addQueue(data& d) {
        std::lock_guard<std::mutex> lock(qMutex);
        requestQueue.push(std::move(d));
        cvq.notify_one();
    }

    data QueryClass::popQueue() {
        std::unique_lock<std::mutex> lock(qMutex);
        cvq.wait(lock, [this] {
            return !requestQueue.empty();
        });
        data d = requestQueue.front();
        requestQueue.pop();
        return d;
    }

    //misc necessary
    int QueryClass::idCallback(void *data, int argc, char **argv, char **azColName) {
        if (argc > 0 && argv[0]) {
            int* id = static_cast<int*>(data);
            *id = std::stoi(argv[0]);
            return 0;
        }
        return 1; //no row was found
    }

    int QueryClass::timeCallback(void *data, int argc, char **argv, char **azColName) {
        int* timeData = static_cast<int*>(data);
        if (argc >= 2) {
            utss = std::stoi(argv[0]);  // Set the global utss variable from the first column
            *timeData = std::stoi(argv[1]);  // Set timeUsed from the second column
        }
        return 0; // Success
    }

    int QueryClass::dataCallback(void *data, int argc, char **argv, char **azColName) {
        auto* rDataPtr = static_cast<std::queue<outPTime>*>(data);

        outPTime ptime;
        if (argc >= 6) {
            for (int i = 0; i + 5 < argc; i += 6) {
                ptime.timeUsed = std::stoi(argv[i]);
                ptime.pName = argv[i + 1] ? argv[i + 1] : "";
                ptime.aName = argv[i + 2] ? argv[i + 2] : "";
                ptime.tName = argv[i + 3] ? argv[i + 3] : "";
                ptime.date = argv[i + 4] ? argv[i + 4] : "";
                ptime.cName = argv[i + 5] ? argv[i + 5] : "";

                rDataPtr->push(ptime);
            }
            return 0; // Success
        }
        return 1;
    }

    //handling different tables
    void QueryClass::handleActivity(const std::string &name) {
        //add in error handling if a new activity fails to be inserted and or started
        //should just change the last activity to the active one
        if (currentActivity != 0) //enters if activity is already started
            changeActive(0, currentActivity);
        //goes here once there is no currently active activity
        //enters if activity insertion failed in an unexpected way
        if (!insertActivity(name)) {
            std::cerr << "Failed to insert and/or start activity\n";
            exitCode = 1;
            return;
        }
        //goes here to get the newly activated activites id
        int tempID = currentActivity;
        if (!getActivityID(name)) {
            std::cerr << "Failed to get activity ID\n";
            currentActivity = tempID;
            exitCode = 1;
            return;
        }
        if (!changeActive(1, currentActivity)){
            std::cerr << "Failed to change active\n";
            exitCode = 1;
        }
   }

    void QueryClass::handlePTime(data& d) {
        inPTime ptimeData;
        ptimeData.date = d.date; //sets date
        ptimeData.utsStart = d.time; //sets time
        insertProgram(d); //inserts the program if it doesn't exist
        ptimeData.pid = getProgramID(d.programName); //gets the program ID using the name of the program
        ptimeData.aid = currentActivity; //sets the current activity

        //if lastPTimeID is not zero, edit the current pTime by changing total time
        if (lastPTimeID != 0) {
            editPtime(lastPTimeID, d.time); // Assume this function uses sqlite3_exec
        }

        if (getCurrentPTimeID(ptimeData)) { //if pTime exists, update utsStart
            std::string sql = "UPDATE pTime SET utsStart = " + std::to_string(d.time) + " WHERE id = " + std::to_string(lastPTimeID) + ";";
            char* errMsg = nullptr;

            try {
                int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
                if (rc != SQLITE_OK) {
                    throw rc; // Throw the SQLite error code
                }
            } catch (int ecode) {
                //handle errors
                std::string err(errMsg);
                sqlite3_free(errMsg); //free the error message memory
                handleError(ecode, err,"handlePTime()");
                return;
            }

            //get the current pTime id
            getCurrentPTimeID(ptimeData);
            return;
        }

        //insert new pTime if it doesn't exist
        insertPtime(ptimeData);
        getCurrentPTimeID(ptimeData);
    }

    //editing rows in tables
    bool QueryClass::changeActive(int act, int &id) {
        char* errMsg = nullptr;
        std::string sql = "UPDATE activity SET active = " + std::to_string(act) + " WHERE id = " + std::to_string(id) + ";";

        try {
            // Execute SQL statement
            int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK) {
                throw rc; //throw the sqlite error code
            }
            return true;
        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            handleError(ecode, err, "changeActive()");
            exitCode = 1;
            if (errMsg) {
                sqlite3_free(errMsg); //free the error message memory
            }
            return false;
        }
    }

    void QueryClass::editPtime(int &id, int &uts) {
        int tu = getTimeUsed(id) + (uts - utss); //calculate the updated time used

        char* errMsg = nullptr;
        std::string sql = "UPDATE pTime SET timeUsed = " + std::to_string(tu) + " WHERE id = " + std::to_string(id) + ";";

        try {
            //execute the sql statement
            int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK) {
                throw rc; //throw the sqlite error code
            }
        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err,"editPTime()");
            exitCode = 1;
            return;
        }

    }

    //inserting rows
    bool QueryClass::insertActivity(const std::string &name) {
        char* errMsg = nullptr;
        //construct the SQL statement
        std::string sql = "INSERT OR IGNORE INTO activity (name, active) VALUES ('" + name + "', 1);";

        try {
            //execute SQL statement
            int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK) {
                std::string errorMsg = errMsg ? errMsg : "Unknown error occurred.";
                throw rc; //throw the sqlite error code
            }

            aChange = true;
            return true;

        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); // Free the error message memory
            handleError(ecode, err, "insertActivity()");
            return false;
        }
    }

    void QueryClass::insertProgram(const data& d) {
        char* errMsg = nullptr;

        //insert type and class into database
        insertType(d.typeName);
        int typeID = getTypeID(d.typeName);
        insertClass(d.className, typeID);
        int classID = getClassID(d.className);

        //construct the sql statement
        std::string sql = "INSERT OR IGNORE INTO program (name, class) VALUES ('" + d.programName + "', " + std::to_string(classID) + ");";

        try {
            //execute sql statement
            int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK) {
                std::string errorMsg = errMsg ? errMsg : "Unknown error occurred.";
                throw rc; //throw the sqlite error code
            }
        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err, "insertProgram()");
        }

    }

    void QueryClass::insertType(const std::string & type) {
        char* errMsg = nullptr;
        //construct sql statement
        std::string sql = "INSERT OR IGNORE INTO type (name) VALUES ('" + type + "');";

        try {
            //execute sql statement
            int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK) {
                throw rc; //throw the sqlite error code
            }
        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err,"insertType()");
        }
    }

    void QueryClass::insertClass(const std::string & cName, int tid) {
        char* errMsg = nullptr;
        //construct sql statement
        std::string sql = "INSERT OR IGNORE INTO class (name, type) VALUES ('" + cName + "', " + std::to_string(tid) + ");";

        try {
            //execute sql statement
            int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK) {
                throw rc; //throw the sqlite error code
            }
        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err,"insertClass()");
        }
    }

    void QueryClass::insertPtime(inPTime & pt) {
        char* errMsg = nullptr;
        //construct sql statement
        std::string sql = "INSERT OR IGNORE INTO pTime (program, utsStart, timeUsed, activity, date) VALUES ("
                          + std::to_string(pt.pid) + ", "
                          + std::to_string(pt.utsStart) + ", 0, "
                          + std::to_string(pt.aid) + ", '"
                          + pt.date + "');";

        try {
            //execute sql statement
            int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
            if (rc != SQLITE_OK) {
                throw rc; //throw the sqlite error code
            }
        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err,"insertPtime()");
        }

    }

    //get information from database
    int QueryClass::getID(std::string query, char* &errMsg) {

        int retID = 0;

        //execute SQL statement
        int rc = sqlite3_exec(db, query.c_str(), idCallback, &retID, &errMsg);
        if (rc != SQLITE_OK) {
            throw rc; //throw the sqlite error code
        }

        return retID;
    }

    bool QueryClass::getActivityID(const std::string &name) {
        char* errMsg = nullptr;
        currentActivity = 0;
        //construct the sql statement
        std::string sql = "SELECT id FROM activity WHERE name = '" + name + "';";

        try {
            currentActivity = getID(sql, errMsg);
            //check if a result was found
            if (currentActivity == 0) {
                std::cerr << "No activity found with name " << name << std::endl;
                return false;
            }

            return true;

        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err, "getActivityID()");
            return false;
        }
    }

    int QueryClass::getProgramID(const std::string & name) {
        char* errMsg = nullptr;
        int programID = 0; //default value for no result

        //construct the sql statement
        std::string sql = "SELECT id FROM program WHERE name = '" + name + "';";

        try {
            //execute sql statement
            programID = getID(sql, errMsg);

            //check if a result was found
            if (programID == 0) {
                std::cerr << "No program found with name " << name << std::endl;
            }

            return programID;

        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err,"getProgramID()");
            return 0;
        }
    }

    int QueryClass::getTypeID(const std::string & name) {
        char* errMsg = nullptr;
        int typeID = 0; //default value for no result

        //construct the sql statement
        std::string sql = "SELECT id FROM program WHERE name = '" + name + "';";

        try {
            //execute sql statement
            typeID = getID(sql, errMsg);

            //check if a result was found
            if (typeID == 0) {
                std::cerr << "No type found with name " << name << std::endl;
            }

            return typeID;

        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err,"getTypeID()");
            return 0;
        }
    }

    int QueryClass::getClassID(const std::string & name) {
        char* errMsg = nullptr;
        int classID = 0; //default value for no result

        //construct the sql statement
        std::string sql = "SELECT id FROM program WHERE name = '" + name + "';";

        try {
            //execute sql statement
            classID = getID(sql, errMsg);

            //check if a result was found
            if (classID == 0) {
                std::cerr << "No class found with name " << name << std::endl;
            }

            return classID;

        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err,"getClassID()");
            return 0;
        }
    }

    bool QueryClass::getCurrentPTimeID(inPTime & pt) {
        char* errMsg = nullptr;
        int pTimeID = 0; //default value for no result

        //construct the sql statement
        std::string sql = "SELECT id FROM pTime WHERE program = " + std::to_string(pt.pid) +
                          " AND date = '" + pt.date +
                          "' AND activity = " + std::to_string(pt.aid) + ";";

        try {
            //execute sql statement
            pTimeID = getID(sql, errMsg);

            //check if a result was found
            if (pTimeID == 0) {
                std::cerr << "Current pTime not found with given parameters: pid: " << pt.pid << " date: " << pt.date << " activity: " << pt.aid << std::endl;
            }

            return pTimeID;

        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err,"getCurrentPTimeID()");
            return 0;
        }
    }

    int QueryClass::getTimeUsed(int id) {
        char* errMsg = nullptr;
        int pTimeID = 0; //default value for no result

        //construct the sql statement
        std::string sql = "SELECT utsStart, timeUsed FROM pTime WHERE id = " + std::to_string(id) + ";";

        try {
            //execute sql statement
            pTimeID = getID(sql, errMsg);

            //check if a result was found
            if (pTimeID == 0) {
                std::cerr << "No pTime found with id: " << id << std::endl;
            }

            return pTimeID;

        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err,"getTimeUsed()");
            return 0;
        }
    }

    void QueryClass::storeDataDate(std::string date) {
        std::lock_guard<std::mutex> lock(rMutex);

        char* errMsg = nullptr;
        //construct the sql statement
        std::string sql = R"(
        SELECT pTime.timeUsed, program.name, activity.name, type.name, pTime.date, class.name
        FROM ptime
        JOIN program ON pTime.program = program.id
        JOIN activity ON pTime.activity = activity.id
        JOIN class ON program.class = class.id
        JOIN type ON class.type = type.id
        WHERE pTime.date >= ')" + date + "';";


        try {
            //execute sql statement
            int rc = sqlite3_exec(db, sql.c_str(), dataCallback, &rData, &errMsg);
            if (rc != SQLITE_OK) {
                throw rc; //throw the SQLite error code
            }
            std::cerr << "No more pTime data found \n";
        } catch (int ecode) {
            //handle errors
            std::string err(errMsg);
            sqlite3_free(errMsg); //free the error message memory
            handleError(ecode, err,"storeDataDate()");
        }

        cvr.notify_one(); //notify the condition variable
    }


}