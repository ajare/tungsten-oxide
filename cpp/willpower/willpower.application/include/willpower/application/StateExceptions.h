#pragma once

#include <string>
#include <exception>

#include "willpower/application/Platform.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		/**	\class StateException
		*   \brief Base class for state flow control
		*/
		class StateException : public std::exception
		{
		public:

			/**	\brief Constructor.
			*/
			StateException()
				: std::exception()
			{
			}
		};

		/**	\class MoveToStateException
		*   \brief Used to move from one State to another
		*/
		class MoveToStateException : public StateException
		{
			std::string mNextState;

			void* mArgs;

		public:

			/**	\brief Constructor.
			*
			*	\param nextState name of next State
			*	\param args arguments to pass to next State's _enter method
			*/
			MoveToStateException(std::string const& nextState, void* args = nullptr)
				: StateException()
				, mNextState(nextState)
				, mArgs(args)
			{
			}

			/**	\brief Returns the name of the State to move to
			*
			*	\return the State name
			*/
			std::string const& getNextState() const
			{
				return mNextState;
			}

			/**	\brief Returns the arguments to pass to the next State
			*
			*	\return the arguments
			*/
			void* getArguments() const
			{
				return mArgs;
			}
		};

		/**	\class SuspendAndMoveToStateException
		*   \brief Used to suspend current State, and move to another.
		*/
		class SuspendAndMoveToStateException : public StateException
		{
			std::string mNextState;

			bool mSuspendEvents, mSuspendUpdate, mSuspendRender;

			void* mwArgs;

		public:

			/**	\brief Constructor.
			*
			*	\param nextState name of next State
			*	\param suspendEvents whether to suspend event handling of current State
			*	\param suspendUpdate whether to suspend logic processing of current State
			*	\param suspendRender whether to suspend rendering of current State
			*	\param args arguments to pass to next State's _enter method
			*/
			SuspendAndMoveToStateException(std::string const& nextState, bool suspendEvents, bool suspendUpdate, bool suspendRender, void* args = nullptr)
				: StateException()
				, mNextState(nextState)
				, mSuspendEvents(suspendEvents)
				, mSuspendUpdate(suspendUpdate)
				, mSuspendRender(suspendRender)
				, mwArgs(args)
			{
			}

			/**	\brief Returns the name of the State to move to
			*
			*	\return the State name
			*/
			std::string const& getNextState() const
			{
				return mNextState;
			}

			/**	\brief Returns the arguments to pass to the next State
			*
			*	\return the arguments
			*/
			void* getArguments() const
			{
				return mwArgs;
			}

			/**	\brief Returns whether event handling is suspended
			*
			*	\return whether event handling is suspended
			*/
			bool getEventsSuspended() const
			{
				return mSuspendEvents;
			}

			/**	\brief Returns whether logic processing is suspended
			*
			*	\return whether logic processing is suspended
			*/
			bool getUpdateSuspended() const
			{
				return mSuspendUpdate;
			}

			/**	\brief Returns whether rendering is suspended
			*
			*	\return whether rendering is suspended
			*/
			bool getRenderSuspended() const
			{
				return mSuspendRender;
			}
		};

		/**	\class ReturnFromStateException
		*   \brief Used to return from current State to previously-suspended State.
		*
		*	\param args arguments to pass to next State's _enter method
		*/
		class ReturnFromStateException : public StateException
		{
			void* mwArgs;

		public:

			/**	\brief Constructor.
			*/
			explicit ReturnFromStateException(void* args = nullptr)
				: StateException()
				, mwArgs(args)
			{
			}

			/**	\brief Returns the arguments to pass to the next State
			*
			*	\return the arguments
			*/
			void* getArguments() const
			{
				return mwArgs;
			}
		};

	} // application
} // WP_NAMESPACE
