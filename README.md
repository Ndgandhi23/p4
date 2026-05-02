Authors: 
- Asmi Narsay, netID: adn54 
- Neil Gandhi, netID: ndg53

Testing Strategy:
- The main thing we used for testing was the raw executable file given in Canvas
    - Using this, we were able to run all our tests and see the outcome of its results
    - In addition to this, we used debugging statements in our server side terminal 
- We first ran the tests on the examples used in the p4 outlines. 
    - For example: 
    1|NAM|4|Bob|
    1|SET|17|Smiling politely|
    1|MSG|33||Alice|Private message to Alice|
    1|MSG|20||#all|Hello, world!|
    1|WHO|6|Alice| 
    1|WHO|6|Carol| --> ensured that our output would display 
                        that there  was no user named Carol
    1|WHO|5|#all|
    - By running the ones given in the outline, we were able to make sure that our output matched accordingly
- We also ran tests for edge cases:
    - For example:
    Attempting to perform a task before setting the NAM of the user results in an error 5 which is OTHER
    Getting the status of somebody who has not yet set it should result in a "No status" message
    Getting the status of #all and making sure it prints a list
    
- We also ran tests to test the different error codes we were meant to set up
    - Error 0 tests: unreadable messages
        1|NAM|3|Bob| would result in termination because length is 4 not 3

    - Error 1 tests: requesting a screen name that is in use
        User 1: 1|NAM|4|Bob| --> will run and assign user 1 to the name Bob
        User 2: 1|NAM|4|Bob| --> will return error 1 since user 1 is named Bob

    - Error 2 tests: if the user sends a message to or requests inforation about a screen name not currently in use
        Assuming Carol does not exist:
        1|WHO|6|Carol| --> result in an error 2
        1|MSG|21||Carol|Hello, world!| --> results in an error 2

    - Error 3 tests: if the user requests a screen name, sets a status, or sends a message using a character not allowed.
        1|NAM|9|No space| --> since there are no spaces allowed in NAM, this will result in an error 3 message

    - Error 4 tests: if the user requests a screen name, sets a status, or sends a message that is too long.
        1|SET|66|AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA| --> results in an error 4 since it is too long

    - Error 5 tests: represents other, but is only used in one instance in our code
        Attempting to perform a task before setting the NAM of the user results in an error 5 which is OTHER
    
- We also accounted for if the user doesnt not end the message with the terminating bar "|" it will result in an error 0
    1|NAM|3|Bob --> results in an error 0 and terminates connection
    
- We also accounted for it the user enters a length that is shorter than the actual message
    1|NAM|2|Bob| --> results in an error 0 and termination connection