#pragma once

#include <string>

#include "willpower/application/Platform.h"
#include "willpower/application/State.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		/**	\class StateFactory
		*   \brief Base class for creating states
		*/
		class WP_APPLICATION_API StateFactory
		{
			std::string mType;

		public:

			/**	\brief Constructor.
			*
			*	\param type State type.
			*/
			explicit StateFactory(std::string const& type);

			/**	\brief Returns State type.
			*
			*	\return the State type.
			*/
			std::string const& getType() const;

			/**	\brief Returns a new State instance.
			*
			*	\return the State instance.
			*/
			virtual State* createState() = 0;
		};

	} // application
} // WP_NAMESPACE

#define DECLARE_DEFAULT_STATE_FACTORY(state) \
class state##Factory : public wp::application::StateFactory \
{ \
public: \
	state##Factory() : wp::application::StateFactory(#state) {} \
	wp::application::State* createState() { return new state(); } \
};
