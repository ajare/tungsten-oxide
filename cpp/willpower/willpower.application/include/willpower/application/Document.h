#pragma once

#include <string>


#include "willpower/application/Platform.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		/**	\class Document
		 *  \brief Base class for a document with open/close/save/save-as functionality.
		 */
		template<typename T>
		class Document
		{
			std::string mName;

			std::string mFilepath;

			T* mData;

		public:

			/**	\brief Constructor.
			 *
			 *	\param name document name.
			 *  \param data data to encapsulate.
			 */
			Document(std::string const& name, T* data)
				: mName(name)
				, mData(data)
			{
			}

			/** \brief Copy constructor.
			 *
			 *	\param other document to instantiate from.
			 */
			Document(Document const& other)
			{
				mName = other.mName;
				mFilepath = other.mFilepath;

				mData = new T(*other.mData);
			}

			/** \brief Assignment operator constructor.
			 *
			 *	\param other document to instantiate from.
			 *	\return reference for chaining.
			 */
			Document& operator=(Document const& other)
			{
				mName = other.mName;
				mFilepath = other.mFilepath;
				
				delete mData;
				mData = new T(*other.mData);
				
				return *this;
			}

			/**	\brief Destructor.
			 */
			virtual ~Document()
			{
				delete mData;
			}

			/** \brief return mutable underlying data.
			 *
			 * \return underlying data.
			 */
			T* getData()
			{
				return mData;
			}

			/** \brief return non-mutable underlying data.
			 *
			 * \return underlying data.
			 */
			T const* getData() const
			{
				return mData;
			}

			/** \brief Set name.
					 *
					 *	\param name document name.
					 */
			void setName(std::string const& name)
			{
				mName = name;
			}

			/** \brief Get document name.
			  *
			  * \return name
			  **/
			std::string const& getName() const
			{
				return mName;
			}

			void setFilepath(std::string const& filepath)
			{
				mFilepath = filepath;
			}

			std::string const& getFilepath() const
			{
				return mFilepath;
			}

			virtual bool isModified() const
			{
				return mData->isModified();
			}

			virtual bool saveDocument()
			{
				return mData->save();
			}
		};

	} // application
} // WP_NAMESPACE

