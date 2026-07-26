#pragma once

#include <string>
#include <deque>
#include <functional>
#include <memory>


#include "willpower/application/Platform.h"
#include "willpower/application/Document.h"

namespace WP_NAMESPACE
{
	namespace application
	{

		/**	\class DocumentManager
		 *  \brief Manager for documents, with facade functions and undo/redo
		 */
		template<typename T>
		class DocumentManager
		{
		public:

			typedef std::function<bool(Document<T>*)> UndoableAction;

		private:

			std::deque<std::shared_ptr<Document<T>>> mDocumentHistory;

			typename std::deque<std::shared_ptr<Document<T>>>::iterator mWorkingDocument;

			int mHistorySize;

		private:

			void clearHistory()
			{
				mDocumentHistory.clear();
				mWorkingDocument = mDocumentHistory.end();
			}


		public:

			DocumentManager(int historySize)
				: mHistorySize(historySize)
			{
				mWorkingDocument = mDocumentHistory.end();
			}

			~DocumentManager()
			{
				clearHistory();
			}

			int getMaxHistorySize() const
			{
				return mHistorySize;
			}

			int getCurrentHistorySize() const
			{
				return (int)mDocumentHistory.size();
			}

			void clear()
			{
				clearHistory();
			}

			void undo()
			{
				mWorkingDocument++;
				if (mWorkingDocument == mDocumentHistory.end())
				{
					mWorkingDocument--;
				}
			}

			bool openDocument(std::shared_ptr<Document<T>> document)
			{
				if (!closeDocument(false))
				{
					return false;
				}

				mDocumentHistory.push_back(document);
				mWorkingDocument = mDocumentHistory.begin();
				return true;
			}

			bool closeDocument(bool checkIfSaved)
			{
				if (checkIfSaved)
				{
					if (getWorkingDocument()->isModified())
					{
						return false;
					}
				}

				clearHistory();
				return true;
			}

			bool saveDocument()
			{
				auto workingDoc = getWorkingDocument();
				string filepath = workingDoc->getFilepath();

				if (filepath == "")
				{
					return false;
				}

				return workingDoc->saveDocument();
			}

			bool saveDocumentAs(std::string const& filepath)
			{
				auto workingDoc = getWorkingDocument();
				workingDoc->setFilepath(filepath);
				return workingDoc->saveDocument();
			}

			void redo()
			{
				if (mWorkingDocument != mDocumentHistory.begin())
				{
					mWorkingDocument--;
				}
			}

			Document<T>* getWorkingDocument()
			{
				return mWorkingDocument == mDocumentHistory.end() ? nullptr : (*mWorkingDocument).get();
			}

			Document<T> const* getWorkingDocument() const
			{
				return mWorkingDocument == mDocumentHistory.end() ? nullptr : (*mWorkingDocument).get();
			}

			std::shared_ptr<Document<T>> getWorkingDocumentPtr()
			{
				return *mWorkingDocument;
			}

			int getWorkingDocumentIndex()
			{
				std::deque<std::shared_ptr<Document<T>>>::iterator begin = mDocumentHistory.begin();
				return std::distance<>(begin, mWorkingDocument);
			}

			void undoableAction(UndoableAction action)
			{
				auto workingDoc = getWorkingDocumentPtr();
				auto thisDoc = new Document<T>(*workingDoc);

				if (action(workingDoc.get()))
				{
					// Remove any rooms ahead of current room
					auto it = mDocumentHistory.begin();
					while (it != mWorkingDocument)
					{
						mDocumentHistory.pop_front();
						it = mDocumentHistory.begin();
					}

					mDocumentHistory.pop_front();
					mDocumentHistory.push_front(std::shared_ptr<Document<T>>(thisDoc));
					mDocumentHistory.push_front(workingDoc);

					// Trim list
					while ((int)mDocumentHistory.size() > mHistorySize)
					{
						mDocumentHistory.pop_back();
					}

					mWorkingDocument = mDocumentHistory.begin();
				}
				else
				{
					delete thisDoc;
				}
			}

		};

	} // application
} // WP_NAMESPACE

#pragma once
