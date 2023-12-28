#define _XOPEN_SOURCE
#include <cmath>
#include <mpi.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <sstream>
#include <crypt.h>
#include <vector>
#include <cstring>

using namespace std;

vector<char *> split_string(const char *str, const char delim);

int main(int argc, char *argv[])
{
    const int progressBarWidth = 50;
    int correctCount = 0;
    int shadowCount = 1000;
    int commsz, rank;   // MPI rank and Commsize
    int wordsCount = 0; // holds count of the number of words in the file words
    int rCount;
    int wordsPerProcess = 0;
    int offset = 0;

    bool found = false;
    bool stopComputation = false;

    char line[256];        // variable to store lines from words file (assuming a maximum line length of 256 characters)
    char shadowLine[256];  // variable to store line from shadow file
//     char currentWord[256]; // variable to hold the current dictionary word being tested
    char username[256];
    char salt[256];
    char hashPass[256];
    char rUsername[256];
    char rSalt[256];
    char rHashPass[256];
    char newline = '\n';

    char **wordsArray = nullptr;  // dynamic array to store dictionary
    char **shadowArray = nullptr; // dynamic array to store shadow lines
    char **receivedWordsArray = nullptr;

    string guess; // variable that holds the password guess

    /* Start up MPI */
    MPI_Init(NULL, NULL);
    MPI_Comm_size(MPI_COMM_WORLD, &commsz);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
//     MPI_Request request;
    MPI_Status status;
    MPI_File foundFile;
    MPI_File notFoundFile;

    // copy overs words and shadow files into arrays
    if (rank == 0)
    {
        // Open file words
        ifstream wordsFile("words");
        if (!wordsFile.is_open())
        {
            cerr << "Error: Unable to open words file." << endl;
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        // Get line count from words file
        while (wordsFile.getline(line, sizeof(line)))
        {
            wordsCount++;
        }
        wordsArray = new char *[wordsCount]; // Allocate array

        // Reset the file pointer to the beginning of the file
        wordsFile.clear();
        wordsFile.seekg(0, ios::beg);

        // Read the entire "words" file line by line into the array
        for (int i = 0; i < wordsCount; ++i)
        {
            wordsFile.getline(line, sizeof(line));
            wordsArray[i] = new char[strlen(line) + 1];
            strcpy(wordsArray[i], line);
        }

        // Open the "shadow" file
        ifstream shadowFile("shadow");
        if (!shadowFile.is_open())
        {
            cerr << "Error: Unable to open shadow file." << endl;
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
        shadowArray = new char *[shadowCount]; // Allocate array

        // Reset the file pointer to the beginning of the file
        shadowFile.clear();
        shadowFile.seekg(0, ios::beg);

        // Read the entire "shadow" file line by line into the array
        for (int i = 0; i < shadowCount; ++i)
        {
            shadowFile.getline(shadowLine, sizeof(shadowLine));
            shadowArray[i] = new char[strlen(shadowLine) + 1];
            strcpy(shadowArray[i], shadowLine);
        }

        wordsFile.close();
        shadowFile.close();
    }

    if (rank == 0)
    {
        // Distribute words to other processes
        wordsPerProcess = wordsCount / commsz;
        int remainder = wordsCount % commsz;

        for (int dest = 1; dest < commsz; ++dest)
        {
            int count = wordsPerProcess + (dest <= remainder ? 1 : 0);

            // Send the count of words
            MPI_Send(&count, 1, MPI_INT, dest, 0, MPI_COMM_WORLD);

            // Send the words data
            for (int i = 0; i < count; ++i)
            {
                MPI_Send(wordsArray[offset + i], strlen(wordsArray[offset + i]) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
            }

            offset += count;
        }

        // Process 0 keeps its portion of words
        wordsPerProcess += (remainder > 0 ? 1 : 0);
    }
    else
    {
        // Receive the count of words
        MPI_Recv(&rCount, 1, MPI_INT, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        // Receive the words data
        receivedWordsArray = new char *[rCount];
        for (int i = 0; i < rCount; ++i)
        {
            receivedWordsArray[i] = new char[256]; // Assuming a maximum word length of 256 characters
            MPI_Recv(receivedWordsArray[i], 256, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }
    MPI_Bcast(&wordsPerProcess, 1, MPI_INT, 0, MPI_COMM_WORLD);
    double starttime = MPI_Wtime();
    shadowCount = 5;

    for (int k = 0; k < shadowCount; k++)
    {
        MPI_File_open(MPI_COMM_WORLD, "found.txt", MPI_MODE_RDWR | MPI_MODE_CREATE, MPI_INFO_NULL, &foundFile);
        MPI_File_open(MPI_COMM_WORLD, "notfound.txt", MPI_MODE_RDWR | MPI_MODE_CREATE, MPI_INFO_NULL, &notFoundFile);
        MPI_Bcast(&wordsPerProcess, 1, MPI_INT, 0, MPI_COMM_WORLD);
        if (rank == 0)
        {

            vector<char *> tokens = split_string(shadowArray[k], '$'); // split by dollar sign to split up salt and hash
            strcpy(username, tokens[0]);
            strcpy(salt, "$");
            strcat(salt, tokens[1]);
            strcat(salt, "$");
            strcat(salt, tokens[2]);
            strcat(salt, "$");
            strcpy(hashPass, salt);
            strcat(hashPass, tokens[3]);

            for (int dest = 1; dest < commsz; ++dest)
            {
                MPI_Send(&username, strlen(username) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
                MPI_Send(&salt, strlen(username) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
                MPI_Send(&hashPass, strlen(hashPass) + 1, MPI_CHAR, dest, 0, MPI_COMM_WORLD);
            }
            for (int i = 0; i < wordsPerProcess; i++)
            {
                guess = crypt(wordsArray[offset + i], salt);
                if (guess == hashPass)
                {
                    correctCount++;
                    found = true;
                    stopComputation = 1;

                    // Code for writing from master thread.  Small bug, some seg faults when this is running //
//                     offset = sizeof(rUsername) + sizeof(receivedWordsArray[i] + 1);
//                     char *output = new char[offset];
//                     output = strcat(output, rUsername);
//                     output = strcat(output, receivedWordsArray[i]);
//                     MPI_File_seek(foundFile, 0, MPI_SEEK_END);
//                     MPI_File_write(foundFile, output, strlen(output), MPI_CHAR, MPI_STATUS_IGNORE);
//                     MPI_File_seek(foundFile, 0, MPI_SEEK_END);
//                     MPI_File_write(foundFile, &newline, 1, MPI_CHAR, MPI_STATUS_IGNORE);
//                     delete [] output;
//                     break; // Stop the loop if a password is found
                }
            }
        }

        else
        {
            MPI_Recv(&rUsername, 30, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&rSalt, 30, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&rHashPass, 256, MPI_CHAR, 0, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            for (int i = 0; i < wordsPerProcess; i++)
            {
                guess = crypt(receivedWordsArray[i], rSalt);
                if (guess == rHashPass)
                {
                    found = true;
                    correctCount++;
                    offset = sizeof(rUsername) + sizeof(receivedWordsArray[i] + 1);
                    char *output = new char[offset];
                    output = strcat(output, rUsername);
                    output = strcat(output, receivedWordsArray[i]);
                    MPI_File_seek(foundFile, 0, MPI_SEEK_END);
                    MPI_File_write(foundFile, output, strlen(output), MPI_CHAR, MPI_STATUS_IGNORE);
                    MPI_File_seek(foundFile, 0, MPI_SEEK_END);
                    MPI_File_write(foundFile, &newline, 1, MPI_CHAR, MPI_STATUS_IGNORE);
                    int stopComputationInt = stopComputation ? 1 : 0;
                    MPI_Send(&stopComputationInt, 1, MPI_INT, 0, 1, MPI_COMM_WORLD);
                    delete [] output;
                    break; // Stop the loop if a password is found
                }
                if (found)
                {
                    int stopComputationInt = stopComputation ? 1 : 0;
                    MPI_Send(&stopComputationInt, 1, MPI_INT, 0, 1, MPI_COMM_WORLD);
                    break; // Stop the loop if a password is found
                }
            }
        }

        if (rank != 0)
        {
            for (int src = 1; src < commsz; ++src)
            {
                int stopComputationInt = 0;
                MPI_Iprobe(src, 1, MPI_COMM_WORLD, &stopComputationInt, &status);
                stopComputation = (stopComputationInt != 0);
                if (stopComputation)
                {
//                     cout << "Password found. Stopping computation." << endl;
                    MPI_Recv(&stopComputationInt, 1, MPI_INT, src, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                    break;
                }
            }
        }

        if (stopComputation)
        {
            break; // Stop the loop if a password is found
        }
        MPI_Barrier(MPI_COMM_WORLD);
        if (rank == 0 && !stopComputation)
        {
            MPI_File_seek(notFoundFile, 0, MPI_SEEK_END);
            MPI_File_write(notFoundFile, username, strlen(username), MPI_CHAR, MPI_STATUS_IGNORE);
            MPI_File_seek(notFoundFile, 0, MPI_SEEK_END);
            MPI_File_write(notFoundFile, &newline, 1, MPI_CHAR, MPI_STATUS_IGNORE);

            float progress = static_cast<float>(k + 1) / shadowCount;
            int progressBarLength = static_cast<int>(progress * progressBarWidth);

            cout << "\r[";
            for (int j = 0; j < progressBarLength; ++j)
                cout << "=";
            for (int j = progressBarLength; j < progressBarLength; ++j)
                cout << " ";

            cout << "] " << fixed << setprecision(1) << progress * 100 << "%";
            cout.flush();
        }
    }

    double endtime = MPI_Wtime();
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_File_close(&foundFile);
    MPI_File_close(&notFoundFile);

    if (rank == 0)
        cout << "\nTime taken for " << shadowCount << " passwords is : " << endtime - starttime << " seconds.";

    if (rank != 0)
    {
        for (int i = 0; i < rCount; ++i)
        {
            delete[] receivedWordsArray[i];
        }
        delete[] receivedWordsArray;
    }

    if (rank == 0)
    {
        for (int i = 0; i < wordsCount; ++i)
        {
            delete[] wordsArray[i];
        }
        delete[] wordsArray;

        for (int i = 0; i < 1000; ++i)
        {
            delete[] shadowArray[i];
        }
        delete[] shadowArray;
    }

    MPI_Finalize();

    return 0;
};

vector<char *> split_string(const char *str, const char delim)
{
    vector<char *> tokens;
    char *token = strtok(const_cast<char *>(str), &delim); // Use strtok to split the string

    while (token != nullptr)
    {
        tokens.push_back(token);
        token = strtok(nullptr, &delim);
    }

    return tokens;
}
