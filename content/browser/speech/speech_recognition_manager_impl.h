// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SPEECH_SPEECH_RECOGNITION_MANAGER_IMPL_H_
#define CONTENT_BROWSER_SPEECH_SPEECH_RECOGNITION_MANAGER_IMPL_H_

#include <map>
#include <memory>
#include <string>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/memory/weak_ptr.h"
#include "content/browser/renderer_host/media/media_stream_requester.h"
#include "content/public/browser/speech_recognition_event_listener.h"
#include "content/public/browser/speech_recognition_manager.h"
#include "content/public/browser/speech_recognition_session_config.h"
#include "content/public/browser/speech_recognition_session_context.h"
#include "content/public/common/speech_recognition_error.h"

namespace media {
class AudioSystem;
}

namespace content {
class BrowserMainLoop;
class MediaStreamManager;
class MediaStreamUIProxy;
class SpeechRecognitionManagerDelegate;
class SpeechRecognizer;

// This is the manager for speech recognition. It is a single instance in
// the browser process and can serve several requests. Each recognition request
// corresponds to a session, initiated via |CreateSession|.
//
// In any moment, the manager has a single session known as the primary session,
// |primary_session_id_|.
// This is the session that is capturing audio, waiting for user permission,
// etc. There may also be other, non-primary, sessions living in parallel that
// are waiting for results but not recording audio.
//
// The SpeechRecognitionManager has the following responsibilities:
//  - Handles requests received from various render views and makes sure only
//    one of them accesses the audio device at any given time.
//  - Handles the instantiation of SpeechRecognitionEngine objects when
//    requested by SpeechRecognitionSessions.
//  - Relays recognition results/status/error events of each session to the
//    corresponding listener (demuxing on the base of their session_id).
//  - Relays also recognition results/status/error events of every session to
//    the catch-all snoop listener (optionally) provided by the delegate.
class CONTENT_EXPORT SpeechRecognitionManagerImpl :
    public NON_EXPORTED_BASE(SpeechRecognitionManager),
    public SpeechRecognitionEventListener {
 public:
  // Returns the current SpeechRecognitionManagerImpl or NULL if the call is
  // issued when it is not created yet or destroyed (by BrowserMainLoop).
  static SpeechRecognitionManagerImpl* GetInstance();

  // SpeechRecognitionManager implementation.
  int CreateSession(const SpeechRecognitionSessionConfig& config) override;
  void StartSession(int session_id) override;
  void AbortSession(int session_id) override;
  void AbortAllSessionsForRenderProcess(int render_process_id) override;
  void AbortAllSessionsForRenderView(int render_process_id,
                                     int render_view_id) override;
  void StopAudioCaptureForSession(int session_id) override;
  const SpeechRecognitionSessionConfig& GetSessionConfig(
      int session_id) const override;
  SpeechRecognitionSessionContext GetSessionContext(
      int session_id) const override;
  int GetSession(int render_process_id,
                 int render_view_id,
                 int request_id) const override;

  // SpeechRecognitionEventListener methods.
  void OnRecognitionStart(int session_id) override;
  void OnAudioStart(int session_id) override;
  void OnEnvironmentEstimationComplete(int session_id) override;
  void OnSoundStart(int session_id) override;
  void OnSoundEnd(int session_id) override;
  void OnAudioEnd(int session_id) override;
  void OnRecognitionEnd(int session_id) override;
  void OnRecognitionResults(int session_id,
                            const SpeechRecognitionResults& result) override;
  void OnRecognitionError(int session_id,
                          const SpeechRecognitionError& error) override;
  void OnAudioLevelsChange(int session_id,
                           float volume,
                           float noise_volume) override;

  SpeechRecognitionManagerDelegate* delegate() const { return delegate_.get(); }

 protected:
  // BrowserMainLoop is the only one allowed to istantiate and free us.
  friend class BrowserMainLoop;
  // Needed for dtor.
  friend std::default_delete<SpeechRecognitionManagerImpl>;
  SpeechRecognitionManagerImpl(media::AudioSystem* audio_system,
                               MediaStreamManager* media_stream_manager);
  ~SpeechRecognitionManagerImpl() override;

 private:
  // Data types for the internal Finite State Machine (FSM).
  enum FSMState {
    SESSION_STATE_IDLE = 0,
    SESSION_STATE_CAPTURING_AUDIO,
    SESSION_STATE_WAITING_FOR_RESULT,
    SESSION_STATE_MAX_VALUE = SESSION_STATE_WAITING_FOR_RESULT
  };

  enum FSMEvent {
    EVENT_ABORT = 0,
    EVENT_START,
    EVENT_STOP_CAPTURE,
    EVENT_AUDIO_ENDED,
    EVENT_RECOGNITION_ENDED,
    EVENT_MAX_VALUE = EVENT_RECOGNITION_ENDED
  };

  struct Session {
    Session();
    ~Session();

    int id;
    bool abort_requested;
    bool listener_is_active;
    SpeechRecognitionSessionConfig config;
    SpeechRecognitionSessionContext context;
    scoped_refptr<SpeechRecognizer> recognizer;
    std::unique_ptr<MediaStreamUIProxy> ui;
  };

  // Callback issued by the SpeechRecognitionManagerDelegate for reporting
  // asynchronously the result of the CheckRecognitionIsAllowed call.
  void RecognitionAllowedCallback(int session_id,
                                  bool ask_user,
                                  bool is_allowed);

  // Callback to get back the result of a media request. |devices| is an array
  // of devices approved to be used for the request, |devices| is empty if the
  // users deny the request.
  void MediaRequestPermissionCallback(
      int session_id,
      const MediaStreamDevices& devices,
      std::unique_ptr<MediaStreamUIProxy> stream_ui);

  // Entry point for pushing any external event into the session handling FSM.
  void DispatchEvent(int session_id, FSMEvent event);

  // Defines the behavior of the session handling FSM, selecting the appropriate
  // transition according to the session, its current state and the event.
  void ExecuteTransitionAndGetNextState(Session* session,
                                        FSMState session_state,
                                        FSMEvent event);

  // Retrieves the state of the session, enquiring directly the recognizer.
  FSMState GetSessionState(int session_id) const;

  // The methods below handle transitions of the session handling FSM.
  void SessionStart(const Session& session);
  void SessionAbort(const Session& session);
  void SessionStopAudioCapture(const Session& session);
  void ResetCapturingSessionId(const Session& session);
  void SessionDelete(Session* session);
  void NotFeasible(const Session& session, FSMEvent event);

  bool SessionExists(int session_id) const;
  Session* GetSession(int session_id) const;
  SpeechRecognitionEventListener* GetListener(int session_id) const;
  SpeechRecognitionEventListener* GetDelegateListener() const;
  int GetNextSessionID();

  media::AudioSystem* audio_system_;
  MediaStreamManager* media_stream_manager_;
  typedef std::map<int, Session*> SessionsTable;
  SessionsTable sessions_;
  int primary_session_id_;
  int last_session_id_;
  bool is_dispatching_event_;
  std::unique_ptr<SpeechRecognitionManagerDelegate> delegate_;

  // Used for posting asynchronous tasks (on the IO thread) without worrying
  // about this class being destroyed in the meanwhile (due to browser shutdown)
  // since tasks pending on a destroyed WeakPtr are automatically discarded.
  base::WeakPtrFactory<SpeechRecognitionManagerImpl> weak_factory_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_SPEECH_SPEECH_RECOGNITION_MANAGER_IMPL_H_
