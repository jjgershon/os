1. In settings-hw4.txt, put the path for your maker and breaker devices.

2. generate_guesses.sh (round) (breakers amount) - creates rounds for ConcurrentGameTest. specify the amount of rounds and number of breakers participating and 
then the script will create the contents in hw4_settings_tmp.txt. copy the game's details to settings-hw4.txt for the test. Suggestion: the random 
almost surely gurantees the maker always win so try to change the password generated to make sure some breakers win.

3. ConcurrentGameTest should be run with at least 2 cores to better test the driver handling of concurrency.

Good luck!