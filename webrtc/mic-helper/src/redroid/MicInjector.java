package redroid;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.os.Looper;
import java.io.InputStream;
import java.lang.reflect.Method;

/**
 * Feeds the device's microphone from stdin (s16le, 48 kHz, mono), without any
 * change to the image. Run through adb, as the shell user, with app_process.
 *
 * <p>It registers a dynamic audio policy that loops an AudioTrack back into
 * apps' recordings, the mechanism Android keeps for this kind of injection.
 * The policy goes away when stdin closes or the process dies. These APIs are
 * hidden, hence the reflection; they need MODIFY_AUDIO_ROUTING, which the shell
 * user holds.
 */
public final class MicInjector {
  private static final int SAMPLE_RATE = 48000;
  // AudioMixingRule.RULE_MATCH_ATTRIBUTE_CAPTURE_PRESET
  private static final int RULE_CAPTURE_PRESET = 2;
  // AudioMix.ROUTE_FLAG_LOOP_BACK
  private static final int ROUTE_LOOP_BACK = 2;
  private static final String PKG = "android.media.audiopolicy.";
  private static final int[] SOURCES = {
    MediaRecorder.AudioSource.DEFAULT,
    MediaRecorder.AudioSource.MIC,
    MediaRecorder.AudioSource.CAMCORDER,
    MediaRecorder.AudioSource.VOICE_RECOGNITION,
    MediaRecorder.AudioSource.VOICE_COMMUNICATION,
    MediaRecorder.AudioSource.UNPROCESSED,
  };

  private MicInjector() {}

  private static Object call(Object target, String name, Class<?>[] types, Object... args)
      throws ReflectiveOperationException {
    Class<?> cls = target instanceof Class ? (Class<?>) target : target.getClass();
    Method method;
    try {
      method = cls.getMethod(name, types);
    } catch (NoSuchMethodException e) {
      method = cls.getDeclaredMethod(name, types);
    }
    method.setAccessible(true);
    return method.invoke(target instanceof Class ? null : target, args);
  }

  public static void main(String[] args) throws Exception {
    Looper.prepareMainLooper();
    Class<?> ruleClass = Class.forName(PKG + "AudioMixingRule");
    Class<?> mixClass = Class.forName(PKG + "AudioMix");
    Class<?> policyClass = Class.forName(PKG + "AudioPolicy");

    Object ruleBuilder = Class.forName(PKG + "AudioMixingRule$Builder").newInstance();
    for (int source : SOURCES) {
      AudioAttributes.Builder attributes = new AudioAttributes.Builder();
      call(attributes, "setCapturePreset", new Class<?>[] {int.class}, source);
      call(ruleBuilder, "addRule", new Class<?>[] {AudioAttributes.class, int.class},
          attributes.build(), RULE_CAPTURE_PRESET);
    }
    Object rule = call(ruleBuilder, "build", new Class<?>[] {});

    AudioFormat format = new AudioFormat.Builder()
        .setSampleRate(SAMPLE_RATE)
        .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
        .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
        .build();
    Object mixBuilder = Class.forName(PKG + "AudioMix$Builder").getConstructor(ruleClass)
        .newInstance(rule);
    call(mixBuilder, "setFormat", new Class<?>[] {AudioFormat.class}, format);
    call(mixBuilder, "setRouteFlags", new Class<?>[] {int.class}, ROUTE_LOOP_BACK);
    Object mix = call(mixBuilder, "build", new Class<?>[] {});

    Object policyBuilder = Class.forName(PKG + "AudioPolicy$Builder")
        .getConstructor(android.content.Context.class).newInstance((Object) null);
    call(policyBuilder, "addMix", new Class<?>[] {mixClass}, mix);
    call(policyBuilder, "setLooper", new Class<?>[] {Looper.class}, Looper.getMainLooper());
    Object policy = call(policyBuilder, "build", new Class<?>[] {});

    int result = (Integer) call(AudioManager.class, "registerAudioPolicyStatic",
        new Class<?>[] {policyClass}, policy);
    if (result != 0) {
      System.err.println("registerAudioPolicy failed: " + result);
      System.exit(1);
    }
    AudioTrack track =
        (AudioTrack) call(policy, "createAudioTrackSource", new Class<?>[] {mixClass}, mix);
    track.play();
    System.err.println("mic injection started");

    byte[] buffer = new byte[960]; // 10 ms
    InputStream in = System.in;
    int count;
    while ((count = in.read(buffer)) > 0) {
      track.write(buffer, 0, count);
    }
    // Exiting drops the policy: audioserver watches this process' binder.
    System.exit(0);
  }
}
