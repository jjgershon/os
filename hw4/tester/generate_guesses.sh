#!/bin/bash


function RandomizeRound
{
for i in `seq $1`;do
	rnd=$(($RANDOM))
	j=$((rnd%9000+1000))
	printf "$j,"
	done;
}
#first param is breaker_id.2nd is how many rounds
function GenerateBreaker
{
	for i in `seq $2`;do
		str=`RandomizeRound 10`
		echo "breaker${1}round$i=${str:0:${#str}-1}"
		done;
}

if [[ $# -lt 2 ]];then
	echo "Usage:generate_guesses (rounds) (breakers_amount)"
	exit 1
fi
echo "Randomizing round..."
echo "" > hw4_settings_tmp.txt

str=`RandomizeRound $1`

echo "passwords=${str:0:${#str}-1}">>hw4_settings_tmp.txt
echo "num_of_breakers=$2" >> hw4_settings_tmp.txt
for i in `seq 0 $(($2-1))`;do
	GenerateBreaker $i $1 >> hw4_settings_tmp.txt
	done;

echo "Randomization done!"