#ifndef _Q_LEARN_H
#define _Q_LEARN_H

// Based on this https://github.com/mtimjones/QLearning

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <assert.h>
#include <stdlib.h>

#define X_MAX VT_W
#define Y_MAX VT_H

#define MAX_EPOCHS 10000

#define MAX_ACTIONS 4

typedef struct {
   int y;
   int x;
} pos_t;

typedef struct {
   double QVal[ MAX_ACTIONS ];
   double QMax;
} stateAction_t;

#define LEARNING_RATE	0.7	// alpha
#define DISCOUNT_RATE   0.4	// gamma

#define EXPLOIT         0   // Choose best Q
#define EXPLORE         1   // Probabilistically choose best Q

#define getSRand()      ( ( double ) rand( ) / ( double ) RAND_MAX )
#define getRand(x)      ( int )( ( double )( x ) * rand( ) / ( RAND_MAX+1.0 ) )

const pos_t dir[ MAX_ACTIONS ] =
{
  { -1,  0 },  /* N */
  {  0,  1 },  /* E */
  {  1,  0 },  /* S */
  {  0, -1 }   /* W */
};


uint8_t environment[ Y_MAX * X_MAX ] = {0};

static stateAction_t stateSpace[ Y_MAX ][ X_MAX ];

void nhbot_qlearn_set_env(uint8_t env[Y_MAX*X_MAX])
{
    memcpy(environment, env, X_MAX*Y_MAX*sizeof(uint8_t));
}

//
// Return the reward value for the state
//
int getReward( uint8_t input )
{
   switch( input )
   {
      case '.':
         // Obstacle, not a legal move
         return -1;
      case '$':
      case '+':
      case ' ':
      case '#':
         // Goal, legal move, 1 reward
         return 1;
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
int legalMove( int y_state, int x_state, int action )
{
  int y = y_state + dir[ action ].y;
  int x = x_state + dir[ action ].x;

  if ( getReward( environment[ y * X_MAX + x ] ) < 0 ) return 0;
  else return 1;
}

//
// Choose an action based upon the selection policy.
//
int ChooseAgentAction( pos_t *agent, int actionSelection )
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
        if (legalMove( agent->y, agent->x, action )) {
            break;
        }
      }
   }

   return action;
}

//
// Update the agent using the Q-value function.
//
void UpdateAgent( pos_t *agent, int action )
{
   int newy = agent->y + dir[ action ].y;
   int newx = agent->x + dir[ action ].x;

   // Update the agent's position
   if (newx < 0 || newx >= X_MAX ||
       newy < 0 || newy >= Y_MAX) {
       return;
   }

   double reward = (double)getReward( environment[ newy * X_MAX + newx ] );

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

void nhbot_qlearn(pos_t *agent)
{
   for ( int epochs = 0 ; epochs < MAX_EPOCHS ; epochs++ )
   {
      // Select the action for the agent.
      int action = ChooseAgentAction( agent, EXPLORE );

      // Update the agent based upon the action.
      UpdateAgent( agent, action );
   }
}
#endif
