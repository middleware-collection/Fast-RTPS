#include <fastrtps/log/Log.h>
#include <fastrtps/log/StdoutConsumer.h>
#include <iostream>

using namespace std;
namespace eprosima {
namespace fastrtps {

struct Log::Resources Log::mResources;

Log::Resources::Resources():
   mDefaultConsumer(new StdoutConsumer),
   mLogging(false),
   mWork(false),
   mFilenames(false),
   mFunctions(true),
   mVerbosity(Log::Error)
{
}

Log::Resources::~Resources()
{
   Log::KillThread();
}

void Log::RegisterConsumer(std::unique_ptr<LogConsumer> consumer) 
{
   std::unique_lock<std::mutex> guard(mResources.mConfigMutex);
   mResources.mConsumers.emplace_back(std::move(consumer));
}

void Log::Reset()
{
   std::unique_lock<std::mutex> configGuard(mResources.mConfigMutex);
   mResources.mCategoryFilter.reset();
   mResources.mFilenameFilter.reset();
   mResources.mErrorStringFilter.reset();
   mResources.mFilenames = false;
   mResources.mFunctions = true;
   mResources.mVerbosity = Log::Error;
   mResources.mConsumers.clear();
}

void Log::Run() 
{
   std::unique_lock<std::mutex> guard(mResources.mCvMutex);
   while (mResources.mLogging) 
   {
      while (mResources.mWork) 
      {
         mResources.mWork = false;
         guard.unlock();

         mResources.mLogs.Swap();
         while (!mResources.mLogs.Empty())
         {
            std::unique_lock<std::mutex> configGuard(mResources.mConfigMutex);
            if (Preprocess(mResources.mLogs.Front()))
            {
               for (auto& consumer: mResources.mConsumers)
                  consumer->Consume(mResources.mLogs.Front());

               mResources.mDefaultConsumer->Consume(mResources.mLogs.Front());
            }

            mResources.mLogs.Pop();
         }

         guard.lock();
      }

      mResources.mCv.wait(guard);
   }
}

void Log::ReportFilenames(bool report)
{
   std::unique_lock<std::mutex> configGuard(mResources.mConfigMutex);
   mResources.mFilenames = report;
}

void Log::ReportFunctions(bool report)
{
   std::unique_lock<std::mutex> configGuard(mResources.mConfigMutex);
   mResources.mFunctions = report;
}

bool Log::Preprocess(Log::Entry& entry)
{
   if (mResources.mCategoryFilter && !regex_search(entry.context.category, *mResources.mCategoryFilter))
      return false;
   if (mResources.mFilenameFilter && !regex_search(entry.context.filename, *mResources.mFilenameFilter))
      return false;
   if (mResources.mErrorStringFilter && !regex_search(entry.message, *mResources.mErrorStringFilter))
      return false;
   if (!mResources.mFilenames)
      entry.context.filename = nullptr;
   if (!mResources.mFunctions)
      entry.context.function = nullptr;

   return true;
}

void Log::KillThread() 
{
   {
      std::unique_lock<std::mutex> guard(mResources.mCvMutex);
      mResources.mLogging = false;
      mResources.mWork = false;
   }
   if (mResources.mLoggingThread) 
   {
      mResources.mCv.notify_all();

      #ifndef _WIN32 // Workaround for MSVC static destruction
      mResources.mLoggingThread->join();
      #endif
      mResources.mLoggingThread.reset();
   }
}

void Log::QueueLog(const std::string& message, const Log::Context& context, Log::Kind kind)
{
   {
      std::unique_lock<std::mutex> guard(mResources.mCvMutex);
      if (!mResources.mLogging && !mResources.mLoggingThread) 
      {
         mResources.mLogging = true;
         mResources.mLoggingThread.reset(new thread(Log::Run));
      }
   }

   mResources.mLogs.Push(Log::Entry{message, context, kind});
   {
      std::unique_lock<std::mutex> guard(mResources.mCvMutex);
      mResources.mWork = true;
   }
   mResources.mCv.notify_all();
}

Log::Kind Log::GetVerbosity()
{
   return mResources.mVerbosity;
}

void Log::SetVerbosity(Log::Kind kind)
{
   std::unique_lock<std::mutex> configGuard(mResources.mConfigMutex);
   mResources.mVerbosity = kind;
}

void Log::SetCategoryFilter(const std::regex& filter)
{
   std::unique_lock<std::mutex> configGuard(mResources.mConfigMutex);
   mResources.mCategoryFilter.reset(new std::regex(filter));
}

void Log::SetFilenameFilter(const std::regex& filter)
{
   std::unique_lock<std::mutex> configGuard(mResources.mConfigMutex);
   mResources.mFilenameFilter.reset(new std::regex(filter));
}

void Log::SetErrorStringFilter(const std::regex& filter)
{
   std::unique_lock<std::mutex> configGuard(mResources.mConfigMutex);
   mResources.mErrorStringFilter.reset(new std::regex(filter));
}

} //namespace fastrtps 
} //namespace eprosima 
