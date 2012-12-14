#
# rsa.rb - RSA encryption and decryption for large data.
#

require 'openssl'

class OpenSSL::PKey::RSA
  def private_encrypt_block(s, pad=PKCS1_PADDING)
    s.scan(/.{1,128}/m).inject([]) do |arr, substr|
      arr << private_encrypt(substr, pad)
    end
  end

  def public_decrypt_block(arr, pad=PKCS1_PADDING)
    arr.map { |s| private_decrypt(s, pad) }.join('')
  end

  def public_encrypt_block(s, pad=PKCS1_PADDING)
    s.scan(/.{1,128}/m).inject([]) do |arr, substr|
      arr << public_encrypt(substr, pad)
    end
  end

  def private_decrypt_block(arr, pad=PKCS1_PADDING)
    arr.map { |s| private_decrypt(s, pad) }.join('')
  end
end
