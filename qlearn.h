#ifndef _Q_LEARN_H
#define _Q_LEARN_H

// Based on this https://github.com/mtimjones/QLearning

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <stdlib.h>
#include "nhbot.h"

#define X_MAX VT_W
#define Y_MAX VT_H

#define MAX_EPOCHS 100000

#define MAX_ACTIONS 8

typedef struct {
   int y;
   int x;
} pos_t;

typedef struct {
   double QVal[ MAX_ACTIONS ];
   double QMax;
} stateAction_t;

#define LEARNING_RATE	0.8	// alpha
#define DISCOUNT_RATE   0.9	// gamma

#define EXPLOIT         0   // Choose best Q
#define EXPLORE         1   // Probabilistically choose best Q

#define getSRand()      ( ( double ) rand( ) / ( double ) RAND_MAX )
#define getRand(x)      ( int )( ( double )( x ) * rand( ) / ( RAND_MAX+1.0 ) )

const pos_t dir[ MAX_ACTIONS ] =
{
  { -1,  0 },  /* N */
  {  0,  1 },  /* E */
  {  1,  0 },  /* S */
  {  0, -1 },  /* W */

  { -1,  1 },  /* NE */
  { -1, -1 },  /* NW */
  {  1,  1 },  /* SE */
  {  1, -1 }   /* SW */
};


uint8_t environment[ Y_MAX * X_MAX ] = {0};

static stateAction_t stateSpace[ Y_MAX ][ X_MAX ];

void nhbot_qlearn_set_env(NetHackState *nethack_state)
{
    memcpy(environment, nethack_state->ScreenChar, X_MAX*Y_MAX*sizeof(uint8_t));
}


//
// Return the reward value for the state
//
int getReward(NetHackState *nethack_state, int x, int y)
{
    uint8_t ch = nethack_state->ScreenChar[y * VT_W + x];
    uint8_t c = nethack_state->ScreenColor[y * VT_W + x];
    if (ch == '-' && c == (Brown|0x08)) { return 0; }
    if (ch == '|' && c == (Brown|0x08)) { return 0; }
    switch(ch) {
    case '-':
    case '|':
        return -1;
    case '.':
        return  0;
    case '$':
    case '/':
    case '>':
        return  1;
   }
   return 0;
}

//
// Find and cache the largest Q-value for the state.
//
void CalculateMaxQ( int y, int x )
{
   stateSpace[ y ][ x ].QMax = 0.0;

   for ( int i = 0 ; i < MAX_ACTIONS ; i++ )
   {
      if ( stateSpace[ y ][ x ].QVal[ i ] > stateSpace[ y ][ x ].QMax )
      {
         stateSpace[ y ][ x ].QMax = stateSpace[ y ][ x ].QVal[ i ];
      }
   }
   
   return;
}

//
// Identify whether the desired move is legal.
//
int legalMove(NetHackState *nethack_state, int y_state, int x_state, int action )
{
  int y = y_state + dir[ action ].y;
  int x = x_state + dir[ action ].x;

  if (getReward(nethack_state, x, y) < 0 ) return 0;
  else return 1;
}

//
// Choose an action based upon the selection policy.
//
int ChooseAgentAction(NetHackState *nethack_state, pos_t *agent, int actionSelection )
{
   int action;

   // Choose the best action (largest Q-value)
   if ( actionSelection == EXPLOIT )
   {
      for ( action = 0 ; action < MAX_ACTIONS ; action++ )
      {
         if ( stateSpace[ agent->y ][ agent->x ].QVal[ action ] ==
              stateSpace[ agent->y ][ agent->x ].QMax )
         {
            break;
         }
      }
   }
   // Choose a random action.
   else if ( actionSelection == EXPLORE )
   {
      for (int tries = 0; tries< 100; tries++) {
        action = getRand( MAX_ACTIONS );
        if (legalMove(nethack_state, agent->y, agent->x, action )) {
            break;
        }
      }
   }

   return action;
}

//
// Update the agent using the Q-value function.
//
void UpdateAgent(NetHackState *nethack_state, pos_t *agent, int action )
{
   int newy = agent->y + dir[ action ].y;
   int newx = agent->x + dir[ action ].x;

   // Update the agent's position
   if (newx < 0 || newx >= X_MAX ||
       newy < 0 || newy >= Y_MAX) {
       return;
   }

   double reward = (double)getReward(nethack_state, newx, newy);

   // Evaluate Q value 
   stateSpace[ agent->y ][ agent->x ].QVal[ action ] += 
     LEARNING_RATE * ( reward + ( DISCOUNT_RATE * stateSpace[ newy ][ newx ].QMax) -
                        stateSpace[ agent->y ][ agent->x ].QVal[ action ] );

   CalculateMaxQ( agent->y, agent->x );

   // Update the agent's position
   if (newx >= 0 && newx < X_MAX &&
       newy >= 0 && newy < Y_MAX) {
       agent->x = newx;
       agent->y = newy;
   }

   return;
}

void nhbot_qlearn(NetHackState *nethack_state, pos_t *agent)
{
   for (int epochs = 0; epochs < MAX_EPOCHS; epochs++) {
      int action = ChooseAgentAction(nethack_state, agent, EXPLORE );
      UpdateAgent(nethack_state, agent, action);
   }
}
#endif
