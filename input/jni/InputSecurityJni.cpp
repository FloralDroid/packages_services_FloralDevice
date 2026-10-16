/*
 * Copyright 2026 FloralDroid
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "SelinuxState.h"

#include <jni.h>
#include <string>

extern "C" JNIEXPORT jint JNICALL
Java_com_floraldroid_input_SelinuxStateReader_00024Native_query(JNIEnv* env, jclass) {
  const auto status = floral::input::QuerySelinuxState();
  if (status.state == floral::input::SelinuxState::Unknown) {
    const std::string message = "Cannot query SELinux state (errno=" +
                                std::to_string(status.error) + ")";
    jclass exception = env->FindClass("java/io/IOException");
    if (exception != nullptr) env->ThrowNew(exception, message.c_str());
    return 0;
  }
  return static_cast<jint>(status.state);
}
