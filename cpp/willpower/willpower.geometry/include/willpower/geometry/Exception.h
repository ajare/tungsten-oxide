#pragma once

#include "willpower/common/Exceptions.h"

namespace WP_NAMESPACE
{
	namespace geometry
	{
		class GeometryOperationException : public Exception
		{
			bool mConsistentState;

		public:

			GeometryOperationException(std::string const& operation, std::string const& error, bool consistentState)
				: Exception(operation + ": " + error)
				, mConsistentState(consistentState)
			{
			}

			void setConsistentState(bool consistentState)
			{
				mConsistentState = consistentState;
			}

			bool isConsistentState() const
			{
				return mConsistentState;
			}
		};

		class GeometryOperationInvalidArgument : public GeometryOperationException
		{
		public:

			GeometryOperationInvalidArgument(std::string const& operation, std::string const& arg, std::string const& error)
				: GeometryOperationException(operation, "argument '" + arg + +"': " + error, true)
			{
			}


		};

	} // geometry
} // WP_NAMESPACE

