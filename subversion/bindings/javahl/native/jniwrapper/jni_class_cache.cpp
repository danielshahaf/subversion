/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 */

#include <stdexcept>

#define SVN_JAVAHL_JNIWRAPPER_LOG(expr)
#include "jni_env.hpp"
#include "jni_globalref.hpp"
#include "jni_exception.hpp"
#include "jni_object.hpp"
#include "jni_string.hpp"

#include "../SubversionException.hpp"

namespace Java {

const ClassCache* ClassCache::m_instance = NULL;

void ClassCache::create()
{
  const char* exception_message = NULL;

  try
    {
      new ClassCache(Env());
    }
  catch (const std::exception& ex)
    {
      exception_message = ex.what();
    }
  catch (...)
    {
      exception_message = "Caught unknown C++ exception";
    }

  // Use the raw environment without exception checks here
  ::JNIEnv* const jenv = Env().get();
  if (exception_message || jenv->ExceptionCheck())
    {
      const jclass rtx = jenv->FindClass("java/lang/RuntimeException");
      const jmethodID ctor = jenv->GetMethodID(rtx, "<init>",
                                               "(Ljava/lang/String;"
                                               "Ljava/lang/Throwable;)V");
      jobject cause = jenv->ExceptionOccurred();
      if (!cause && exception_message)
        {
          const jstring msg = jenv->NewStringUTF(exception_message);
          cause = jenv->NewObject(rtx, ctor, msg, jthrowable(0));
        }
      const jstring reason =
        jenv->NewStringUTF("JavaHL native library initialization failed");
      const jobject exception = jenv->NewObject(rtx, ctor, reason, cause);
      jenv->Throw(jthrowable(exception));
    }
}

void ClassCache::destroy()
{
  const ClassCache* const instance = m_instance;
  m_instance = NULL;
  delete instance;
}

ClassCache::~ClassCache() {}

#define SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(M, C)    \
  m_##M(env, env.FindClass(C::m_class_name))
ClassCache::ClassCache(Env env)
  : SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(object, Object),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(classtype, Class),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(throwable, Exception),
    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(string, String),

    SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT(subversion_exception,
                                           ::JavaHL::SubversionException)
{
  m_instance = this;
  // no-op: Object::static_init(env);
  Class::static_init(env);
  Exception::static_init(env);
  // no-op: String::static_init(env);
  // no-op: ::JavaHL::SubversionException::static_init(env);
}
#undef SVN_JAVAHL_JNIWRAPPER_CLASS_CACHE_INIT

} // namespace Java
